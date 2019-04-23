#define BOOST_TEST_DYN_LINK
#include <mpi.h>
#include <boost/serialization/export.hpp>
#include <boost/test/unit_test.hpp>
#include "TaskConst.hpp"
#include "test_helper.hpp"
#include "stdlib.h"
// BOOST_CLASS_EXPORT_IMPLEMENT(TaskConst)

#include "sgpp/distributedcombigrid/sparsegrid/DistributedSparseGridUniform.hpp"

#include "sgpp/distributedcombigrid/combischeme/CombiMinMaxScheme.hpp"
#include "sgpp/distributedcombigrid/loadmodel/LearningLoadModel.hpp"
#include "sgpp/distributedcombigrid/loadmodel/LinearLoadModel.hpp"
#include "sgpp/distributedcombigrid/manager/CombiParameters.hpp"
#include "sgpp/distributedcombigrid/manager/ProcessGroupManager.hpp"
#include "sgpp/distributedcombigrid/manager/ProcessGroupWorker.hpp"
#include "sgpp/distributedcombigrid/manager/ProcessManager.hpp"
#include "sgpp/distributedcombigrid/task/Task.hpp"
#include "sgpp/distributedcombigrid/utils/Config.hpp"
#include "sgpp/distributedcombigrid/utils/Types.hpp"

DistributedSparseGridUniform<CombiDataType> getSmallTestDSG(){
  // set up a dsg for sending
  LevelVector levels = {2, 2};
  const DimType dim = levels.size();
  std::vector<bool> boundary(2, true);
  LevelVector lmin = levels;
  LevelVector lmax = levels;
  for (DimType d = 0; d < dim; ++d) {
    lmax[d] *= 2;
  }
  return DistributedSparseGridUniform<CombiDataType>{dim, lmax, lmin, boundary, MPI_COMM_SELF};
}

void pingPongTest(){
  BOOST_REQUIRE(TestHelper::checkNumMPIProcsAvailable(2));

  CommunicatorType comm = TestHelper::getComm(2);
  if (comm == MPI_COMM_NULL) {
    return;
  }

  DistributedSparseGridUniform<CombiDataType> myDSG = getSmallTestDSG();
  DistributedSparseGridUniform<CombiDataType> myDSGCopy {myDSG};

  // send one way
  if (TestHelper::getRank(comm) == 1){
    RankType src = 0;
    DistributedSparseGridUniform<CombiDataType> * myDSG2 = recvDSGUniform<CombiDataType>(src, (CommunicatorType) comm);
    myDSG = DistributedSparseGridUniform<CombiDataType>(*myDSG2);
  }else{
    sendDSGUniform(&myDSG,1,comm);
  }

  // send the other way and add to existing
  if (TestHelper::getRank(comm) == 0){
    RankType src = 1;
    myDSG.recvAndAddDSGUniform(src, comm);
  }else{
    sendDSGUniform(&myDSG,0,comm);
  }

  // for (size_t i = 0; i < myDSG.getNumSubspaces(); ++i){
  //   BOOST_TEST(myDSG.subspaces_[i].data_ == (myDSGCopy.subspaces_[i].data_ * 2)); //TODO
  // }

  MPI_Barrier(comm);
}

void checkManagerRecv() {
  // Recv algorithm:
  // get handle to process group to write to
  // set up receive channel to middleman
  // after every first combine from manager{
  //  listen & receive
  //  write into extra process group's recv-memory, one after the other process
  //  //  notify manager if completed
  //  notify process of process group of completion, upon which they call the 
  //  broadcast updated dsg
  // }

  // technology considerations If receiver as extra thread: MPI+threads or MPI+MPI(with RMA)?
  // cf wgropp.cs.illinois.edu/courses/cs598-s16/lectures/lecture36.pdf
  // => rather advises towards RMA
}

void checkManagerSend() {
  // Send algorithm:
  // get handle to process group to read from
  // set up send channel to middleman
  // after every first combine from manager{
  //  gather grid
  //  stream it to middleman/other machines
  //  notify process of process group of completion, upon which they call the 
  //  broadcast updated dsg
  // }
}

bool checkGatheredSparseGridFromProcessGroup(ProcessManager* manager = nullptr,
                                           CombiParameters params = CombiParameters(),
                                           size_t nprocs = 1) {
  /*
  size_t numGrids = params.getNumGrids();
  auto& combinedUniDSGVector = manager->getOutboundUniDSGVector();

  bool found = false;
  for (size_t i = 0; i < nprocs; ++i) {
    for (int g = 0; g < numGrids; ++g) {
      const auto& dsgu = combinedUniDSGVector[g];
      BOOST_CHECK(dsgu->getDim() > 0);
      BOOST_CHECK(!dsgu->getBoundaryVector().empty());
      BOOST_CHECK(dsgu->getNMax()[0] >= 0);
      BOOST_CHECK(dsgu->getNumSubspaces() > 0);

      size_t subspaceNo = 0;
      std::cerr << combigrid::toString(dsgu->getDataVector(subspaceNo)) << std::endl;
      auto it = std::find_if(dsgu->getDataVector(subspaceNo).begin(),
                              dsgu->getDataVector(subspaceNo).end(), [](CombiDataType& dve) {
                                return (abs(dve) - 1.11666666666667) < TestHelper::tolerance;
                              });
      if (it != dsgu->getDataVector(subspaceNo).end()) {
        found = true;
      }
    }
  }
  return found;
  */
  return true;
}

void checkAddSparseGridToProcessGroup(ProcessManager* manager = nullptr,
                                      ProcessGroupWorker* pgw = nullptr) {
  /*
  // manager->getInboundUniDSGVector() = manager->getOutboundUniDSGVector();

  if (manager != nullptr) {                                       // manager code
                                                                  // iterate process group
    size_t numGrids = manager->getOutboundUniDSGVector().size();  // TODO inbound

    // iterate first process group
    // for (size_t i = theMPISystem()->getNumProcs(); i < 2 * theMPISystem()->getNumProcs(); ++i) {
    for (size_t i = 0; i < theMPISystem()->getNumProcs(); ++i) {
      for (size_t g = 0; g < numGrids; ++g) {
        sendDSGUniform<CombiDataType>(manager->getOutboundUniDSGVector()[g].get(), i,
                                      theMPISystem()->getWorldComm());
      }
    }

  } else if (pgw != nullptr) {  // worker code
                                // put subspace data into buffer for allreduce
    // // only second process group
    // if (theMPISystem()->getWorldRank() >= theMPISystem()->getNumProcs() &&
    //     theMPISystem()->getWorldRank() < 2 * theMPISystem()->getNumProcs()) {
    // only first process group
    if (theMPISystem()->getWorldRank() < theMPISystem()->getNumProcs()) {
      size_t numGrids = pgw->getCombinedUniDSGVector().size();
      for (size_t g = 0; g < numGrids; ++g) {
        RankType src = theMPISystem()->getManagerRankWorld();
        CommunicatorType comm = theMPISystem()->getWorldComm();
        pgw->getCombinedUniDSGVector()[g]->recvAndAddDSGUniform(src, comm);
      }
    }
  }
  */
}

void runRabbitMQServer() {
  std::cout << "starting rabbitMQ server..." << std::endl;
  system("rabbitmq-server &");
  // give rabbitmq some time to set up
  sleep(10);
}

void runThirdLevelManager() {
  std::cout << "starting thirdLevelManager..." << std::endl;
  system("../examples/gene_distributed_third_level/third_level_manager/run.sh &");
  // give thirdLevelManger some time to set up
  sleep(1);
}

void killInfrastructure() {
  system("killall rabbitmq-server");
  system("killall thirdLevelManager");
}

void startInfrastructure() {
  //runRabbitMQServer();
  runThirdLevelManager();
}

void runOtherSystem(size_t numProcs) {
  std::cout << "starting other system..." << std::endl;
  std::string command = "mpirun -n " + std::to_string(numProcs) +
                        " /home/marci/UNI/HIWI/combi/distributedcombigrid/tests/test_distributedcombigrid_boost "
                        "--run_test=managerSendRecv -- system2 &";
  system(command.c_str());
}

void testGatherAddDSG(size_t ngroup = 1, size_t nprocs = 1) {
  size_t size = ngroup * nprocs + 1;
  BOOST_REQUIRE(TestHelper::checkNumMPIProcsAvailable(size));

  CommunicatorType comm = TestHelper::getComm(size);
  if (comm == MPI_COMM_NULL) {
    return;
  }

  combigrid::Stats::initialize();

  theMPISystem()->initWorldReusable(comm, ngroup, nprocs);
  // theMPISystem()->init(ngroup, nprocs);

  // set up a constant valued distributed fullgrid in the process groups
  WORLD_MANAGER_EXCLUSIVE_SECTION {
    int argc = boost::unit_test::framework::master_test_suite().argc;
    char** argv = boost::unit_test::framework::master_test_suite().argv;

    //
    // Starting other system
    //
    std::string sysName = "system1";
    if (argc == 1) {
      startInfrastructure();
      runOtherSystem(ngroup * nprocs + 1);
    } else {
      sysName = "system2";
    }

    ProcessGroupManagerContainer pgroups;
    for (int i = 0; i < ngroup; ++i) {
      int pgroupRootID(i);
      pgroups.emplace_back(std::make_shared<ProcessGroupManager>(pgroupRootID));
    }

    auto loadmodel = std::unique_ptr<LoadModel>(new LinearLoadModel());

    DimType dim = 2;
    LevelVector lmin(dim, 4);
    LevelVector lmax(dim, 6);

    size_t ncombi = 1;
    std::vector<bool> boundary(dim, false);

    CombiMinMaxScheme combischeme(dim, lmin, lmax);
    // combischeme.createClassicalCombischeme();
    combischeme.createAdaptiveCombischeme();
    std::vector<LevelVector> levels = combischeme.getCombiSpaces();
    std::vector<combigrid::real> coeffs = combischeme.getCoeffs();

    BOOST_REQUIRE(TestHelper::checkNumMPIProcsAvailable(size));

    // create Tasks
    TaskContainer tasks;
    std::vector<int> taskIDs;
    for (size_t i = 0; i < levels.size(); i++) {
      // std::cerr << "task" << i<< std::endl;
      Task* t = new TaskConst(levels[i], boundary, coeffs[i], loadmodel.get());
      BOOST_CHECK(true);

      tasks.push_back(t);
      taskIDs.push_back(t->getID());
    }

    BOOST_CHECK(true);

    IndexVector parallelization = {static_cast<long>(nprocs), 1};
    // create combiparameters
    CombiParameters params(dim, lmin, lmax, boundary, levels, coeffs, taskIDs, ncombi, 1,
                           parallelization, std::vector<IndexType>(0),
                           std::vector<IndexType>(0), "localhost", 9999, sysName);

    // create abstraction for Manager
    ProcessManager manager(pgroups, tasks, params, std::move(loadmodel));

    std::cout << "running first";
    manager.runfirst();
    std::cout << "running combineThirdLevel";
    manager.combineThirdLevel();
    manager.exit();

    if (argc == 1) {
      killInfrastructure();
    }
  }
  else {
    ProcessGroupWorker pgroup;
    SignalType signal = -1;
    signal = pgroup.wait();
    while (signal != EXIT) {
      signal = pgroup.wait();
      std::cout << "Worker with rank " << theMPISystem()->getLocalRank() << " received signal " << signal << std::endl; 
    }
    // std::cerr << "start checkAddSparseGridToProcessGroup" << std::endl;
    // checkAddSparseGridToProcessGroup(nullptr, &pgroup); //TODO check that doubled
  }

  combigrid::Stats::finalize();
  MPI_Barrier(comm);
}

BOOST_AUTO_TEST_SUITE(managerSendRecv)

/*
BOOST_AUTO_TEST_CASE(test_pp, *boost::unit_test::tolerance(TestHelper::tolerance) 
                                 * boost::unit_test::timeout(10)) {
  pingPongTest();
}
*/

/*
BOOST_AUTO_TEST_CASE(test_1, *boost::unit_test::tolerance(TestHelper::tolerance) 
                                 * boost::unit_test::timeout(20)) {
  testGatherAddDSG(1, 1);
}

BOOST_AUTO_TEST_CASE(test_2, *boost::unit_test::tolerance(TestHelper::tolerance) 
                                 * boost::unit_test::timeout(30)) {
  testGatherAddDSG(1, 2);
}

BOOST_AUTO_TEST_CASE(test_3, *boost::unit_test::tolerance(TestHelper::tolerance) 
                                 * boost::unit_test::timeout(30)) {
  testGatherAddDSG(2, 2);
}
*/

BOOST_AUTO_TEST_CASE(test_4, *boost::unit_test::tolerance(TestHelper::tolerance)) {
  size_t ngroup = 1;
  size_t nprocs = 1;

  testGatherAddDSG(ngroup, nprocs);
}

/*
BOOST_AUTO_TEST_CASE(test_5, *boost::unit_test::tolerance(TestHelper::tolerance) 
                                 * boost::unit_test::timeout(40)) {
  testGatherAddDSG(2, 4);
}
*/

BOOST_AUTO_TEST_SUITE_END()
