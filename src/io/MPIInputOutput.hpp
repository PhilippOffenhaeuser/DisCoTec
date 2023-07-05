#pragma once

// to resolve https://github.com/open-mpi/ompi/issues/5157
#define OMPI_SKIP_MPICXX 1
#include <mpi.h>

#include <algorithm>
#include <numeric>

#include "utils/Types.hpp"

namespace combigrid {
static inline std::string getMpiErrorString(int err) {
  int len;
  char errString[MPI_MAX_ERROR_STRING];
  MPI_Error_string(err, errString, &len);
  return std::string(errString);
}
namespace mpiio {

static MPI_Info getNewConsecutiveMpiInfo() {
  // see: https://wickie.hlrs.de/platforms/index.php/MPI-IO
  // to be modified externally e.g. via romio hints
  MPI_Info info = MPI_INFO_NULL;
  MPI_Info_create(&info);
  // do not use this -- force-enables collective buffering!!
  // //  MPI_Info_set(info, "romio_no_indep_rw", "true");

  // disable ROMIO's data-sieving
  MPI_Info_set(info, "romio_ds_read", "disable");
  MPI_Info_set(info, "romio_ds_write", "disable");
  return info;
}

template <typename T>
int writeValuesConsecutive(const T* valuesStart, MPI_Offset numValues, const std::string& fileName,
                           combigrid::CommunicatorType comm, bool replaceExistingFile = false,
                           bool withCollectiveBuffering = false) {
  // get offset in file
  MPI_Offset pos = 0;
  MPI_Exscan(&numValues, &pos, 1, MPI_OFFSET, MPI_SUM, comm);

  MPI_Info info = getNewConsecutiveMpiInfo();
  MPI_Info_set(info, "access_style", "write_once");
  if (withCollectiveBuffering) {
    // enable ROMIO's collective buffering
    MPI_Info_set(info, "romio_no_indep_rw", "enable");
    MPI_Info_set(info, "romio_cb_write", "enable");
    MPI_Info_set(info, "romio_cb_read", "enable");
  } else {
    // disable ROMIO's collective buffering //TODO test
    MPI_Info_set(info, "romio_cb_write", "disable");
    MPI_Info_set(info, "romio_cb_read", "disable");
  }

  // open file
  MPI_File fh;
  int err = MPI_File_open(comm, fileName.c_str(), MPI_MODE_CREATE | MPI_MODE_EXCL | MPI_MODE_WRONLY,
                          info, &fh);
  if (err != MPI_SUCCESS) {
    auto openErrorString = getMpiErrorString(err);
    if (err != MPI_ERR_FILE_EXISTS && openErrorString.find("File exists") == std::string::npos) {
      // there are some weird MPI-IO implementations that define a new error code for "file
      // exists"...
      std::cerr << "potential write/open error: " << std::to_string(err) << " " << openErrorString
                << std::endl;
    }
    if (replaceExistingFile) {
      // file already existed, delete it and create new file
      int mpi_rank;
      MPI_Comm_rank(comm, &mpi_rank);
      if (mpi_rank == 0) {
        MPI_File_delete(fileName.c_str(), MPI_INFO_NULL);
      }
      MPI_Barrier(comm);
      err = MPI_File_open(comm, fileName.c_str(), MPI_MODE_CREATE | MPI_MODE_EXCL | MPI_MODE_WRONLY,
                          info, &fh);
    } else {
      // open file without creating it
      err = MPI_File_open(comm, fileName.c_str(), MPI_MODE_WRONLY, info, &fh);
    }
  }
  if (err != MPI_SUCCESS) {
    std::cerr << "Open error " << fileName << " :" << std::to_string(err) << " "
              << getMpiErrorString(err) << std::endl;
  }

  // write to single file with MPI-IO
  MPI_Datatype dataType = getMPIDatatype(abstraction::getabstractionDataType<T>());
  MPI_Status status;
  err = MPI_File_write_at_all(fh, pos * sizeof(T), valuesStart, static_cast<int>(numValues),
                              dataType, &status);
  if (err != MPI_SUCCESS) {
    std::cerr << err << " in MPI_File_write_at_all" << std::endl;
    std::cerr << getMpiErrorString(err) << std::endl;
  }
#ifndef NDEBUG
  int numWritten = 0;
  MPI_Get_count(&status, dataType, &numWritten);
  if (numWritten != numValues) {
    std::cout << "not written enough: " << numWritten << " instead of " << numValues << std::endl;
    err = ~MPI_SUCCESS;
  }
#endif  // !NDEBUG

  MPI_File_close(&fh);
  MPI_Info_free(&info);
  return (err == MPI_SUCCESS) ? numValues : 0;
}

template <typename T>
int readValuesConsecutive(T* valuesStart, MPI_Offset numValues, const std::string& fileName,
                          combigrid::CommunicatorType comm, bool withCollectiveBuffering = false) {
  MPI_Offset pos = 0;
  MPI_Exscan(&numValues, &pos, 1, MPI_OFFSET, MPI_SUM, comm);

  MPI_Info info = getNewConsecutiveMpiInfo();
  if (withCollectiveBuffering) {
    // enable ROMIO's collective buffering
    MPI_Info_set(info, "romio_no_indep_rw", "enable");
    MPI_Info_set(info, "romio_cb_write", "enable");
    MPI_Info_set(info, "romio_cb_read", "enable");
  } else {
    // disable ROMIO's collective buffering //TODO test
    MPI_Info_set(info, "romio_cb_write", "disable");
    MPI_Info_set(info, "romio_cb_read", "disable");
  }

  // open file
  MPI_File fh;
  int err = MPI_File_open(comm, fileName.c_str(), MPI_MODE_RDONLY, info, &fh);
  if (err != MPI_SUCCESS) {
    std::cerr << err << " while reading OneFileFromDisk " << fileName << std::endl;
    throw std::runtime_error("read: could not open! " + fileName + ": " + getMpiErrorString(err));
  }
#ifndef NDEBUG
  MPI_Offset fileSize = 0;
  MPI_File_get_size(fh, &fileSize);
  if (fileSize < numValues * sizeof(T)) {
    // loud failure if file is too small
    std::cerr << fileSize << " and not " << numValues << std::endl;
    throw std::runtime_error("read: file size too small!");
  }
#endif

  // read from single file with MPI-IO
  MPI_Datatype dataType = getMPIDatatype(abstraction::getabstractionDataType<T>());
  MPI_Status status;
  err = MPI_File_read_at_all(fh, pos * sizeof(T), valuesStart, static_cast<int>(numValues),
                             dataType, &status);
  MPI_File_close(&fh);
  MPI_Info_free(&info);
  if (err != MPI_SUCCESS) {
    // non-failure
    std::cerr << err << " in MPI_File_read_at_all" << std::endl;
    return 0;
  }

#ifndef NDEBUG
  int readcount = 0;
  MPI_Get_count(&status, dataType, &readcount);
  if (readcount < numValues) {
    // loud failure
    std::cerr << "read " << readcount << " and not " << numValues << std::endl;
    throw std::runtime_error("read: not enough data read!");
  }
#endif

  return numValues;
}

template <typename T, typename ReduceFunctionType>
int readReduceValuesConsecutive(T* valuesStart, MPI_Offset numValues, const std::string& fileName,
                                combigrid::CommunicatorType comm, int numElementsToBuffer,
                                ReduceFunctionType reduceFunction,
                                bool withCollectiveBuffering = false) {
  MPI_Offset pos = 0;
  MPI_Exscan(&numValues, &pos, 1, MPI_OFFSET, MPI_SUM, comm);
  MPI_Info info = getNewConsecutiveMpiInfo();
  if (withCollectiveBuffering) {
    // enable ROMIO's collective buffering
    MPI_Info_set(info, "romio_no_indep_rw", "enable");
    MPI_Info_set(info, "romio_cb_write", "enable");
    MPI_Info_set(info, "romio_cb_read", "enable");
  } else {
    // disable ROMIO's collective buffering //TODO test
    MPI_Info_set(info, "romio_cb_write", "disable");
    MPI_Info_set(info, "romio_cb_read", "disable");
  }

  // open file
  MPI_File fh;
  int err = MPI_File_open(comm, fileName.c_str(), MPI_MODE_RDONLY, info, &fh);
  if (err != MPI_SUCCESS) {
    std::cerr << err << " while reducing OneFileFromDisk " << fileName << std::endl;
    throw std::runtime_error("read: could not open! " + getMpiErrorString(err));
  }

  // read from single file with MPI-IO
  MPI_Datatype dataType = getMPIDatatype(abstraction::getabstractionDataType<T>());
  MPI_Status status;
  int readcount = 0;
  std::vector<T> buffer(numElementsToBuffer);
  auto writePointer = valuesStart;
  while (readcount < numValues) {
    // if there is less to read than the buffer length, overwrite numElementsToBuffer
    if (numValues - readcount < numElementsToBuffer) {
      numElementsToBuffer = numValues - readcount;
      buffer.resize(numElementsToBuffer);
    }
    err = MPI_File_read_at_all(fh, pos * sizeof(T), buffer.data(),
                               static_cast<int>(numElementsToBuffer), dataType, &status);
    int readcountIncrement = 0;
    MPI_Get_count(&status, dataType, &readcountIncrement);
    assert(readcountIncrement > 0);
    // reduce with present sparse grid data
    std::transform(buffer.cbegin(), buffer.cend(), writePointer, writePointer, reduceFunction);
    readcount += readcountIncrement;
    pos += readcountIncrement;
    std::advance(writePointer, readcountIncrement);
  }
  MPI_File_close(&fh);
  MPI_Info_free(&info);
  return readcount;
}
}  // namespace mpiio
}  // namespace combigrid
