/*
 * mpimemory.cpp
 */
// to resolve https://github.com/open-mpi/ompi/issues/5157
#define OMPI_SKIP_MPICXX 1
#include <mpi.h>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/serialization/export.hpp>
#include <string>
#include <vector>

// compulsory includes for basic functionality
#include "sgpp/distributedcombigrid/utils/Types.hpp"
// include user specific task. this is the interface to your application

// to allow using test tasks
#define BOOST_CHECK

using namespace combigrid;

// this is necessary for correct function of task serialization
BOOST_CLASS_EXPORT(TaskAdvection)
BOOST_CLASS_EXPORT(StaticFaults)
BOOST_CLASS_EXPORT(WeibullFaults)
BOOST_CLASS_EXPORT(FaultCriterion)

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);

  mpimemory::print_memory_usage_world();
  /* when using timers (TIMING is defined in Stats), the Stats class must be
   * initialized at the beginning of the program. (and finalized in the end)
   */
  Stats::initialize();
  mpimemory::print_memory_usage_world();
  // read in parameter file
  std::string paramfile = "ctparam";
  if (argc > 1) paramfile = argv[1];
  boost::property_tree::ptree cfg;
  boost::property_tree::ini_parser::read_ini(paramfile, cfg);

  // number of process groups and number of processes per group
  size_t ngroup = cfg.get<size_t>("manager.ngroup");
  size_t nprocs = cfg.get<size_t>("manager.nprocs");

  // divide the MPI processes into process group and initialize the
  // corresponding communicators
  theMPISystem()->init(ngroup, nprocs);
  WORLD_MANAGER_EXCLUSIVE_SECTION {
    std::cout << "after mpi system init " << std::flush;
  }

  mpimemory::print_memory_usage_world();

  Stats::finalize();

  /* write stats to json file for postprocessing */
  Stats::write("timers.json");

  WORLD_MANAGER_EXCLUSIVE_SECTION {
    std::cout << "after everything" << std::flush;
  }
  mpimemory::print_memory_usage_world();

  MPI_Finalize();

  return 0;
}
