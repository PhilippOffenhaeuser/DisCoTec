/*
 * TaskExample.hpp
 *
 *  Created on: Sep 25, 2015
 *      Author: heenemo
 */

#ifndef TASKEXAMPLE_HPP_
#define TASKEXAMPLE_HPP_

#include "fullgrid/DistributedFullGrid.hpp"
#include "task/Task.hpp"
#include "fault_tolerance/FTUtils.hpp"

namespace combigrid {

class TaskExample: public Task {

 public:
  /* if the constructor of the base task class is not sufficient we can provide an
   * own implementation. here, we add dt, nsteps, p as a new parameters.
   */
  TaskExample(DimType dim, const LevelVector& l, const std::vector<BoundaryType>& boundary,
              real coeff, LoadModel* loadModel, real dt, size_t nsteps,
              std::vector<int> p = std::vector<int>(0),
              FaultCriterion* faultCrit = (new StaticFaults({0, IndexVector(0), IndexVector(0)})))
      : Task(l, boundary, coeff, loadModel, faultCrit),
        dt_(dt),
        nsteps_(nsteps),
        stepsTotal_(0),
        p_(p),
        initialized_(false),
        combiStep_(0),
        dfg_(NULL) {}

  void init(CommunicatorType lcomm, std::vector<IndexVector> decomposition = std::vector<IndexVector>()) {
    assert(!initialized_);
    assert(dfg_ == NULL);

    int lrank = theMPISystem()->getLocalRank();

    /* create distributed full grid. we try to find a balanced ratio between
     * the number of grid points and the number of processes per dimension
     * by this very simple algorithm. to keep things simple we require powers
     * of two for the number of processes here. */
    int np;
    MPI_Comm_size(lcomm, &np);

    // check if power of two
    if (!((np > 0) && ((np & (~np + 1)) == np)))
      assert(false && "number of processes not power of two");

    DimType dim = this->getDim();
    std::vector<int> p(dim, 1);
    const LevelVector& l = this->getLevelVector();

    if (p_.size() == 0) {
      // compute domain decomposition
      IndexType prod_p(1);

      while (prod_p != static_cast<IndexType>(np)) {
        DimType dimMaxRatio = 0;
        real maxRatio = 0.0;

        for (DimType k = 0; k < dim; ++k) {
          real ratio = std::pow(2.0, l[k]) / p[k];

          if (ratio > maxRatio) {
            maxRatio = ratio;
            dimMaxRatio = k;
          }
        }

        p[dimMaxRatio] *= 2;
        prod_p = 1;

        for (DimType k = 0; k < dim; ++k)
          prod_p *= p[k];
      }
    } else {
      p = p_;
    }

    if (lrank == 0) {
      std::cout << "group " << theMPISystem()->getGlobalRank() << " "
                << "computing task " << this->getID() << " with l = " << this->getLevelVector()
                << " and p = " << p << std::endl;
    }

    // create local subgrid on each process
    dfg_ = new OwningDistributedFullGrid<CombiDataType>(dim, l, lcomm, this->getBoundary(), p);

    /* loop over local subgrid and set initial values */
    auto elements = dfg_->getData();

    for (size_t i = 0; i < dfg_->getNrLocalElements(); ++i) {
      IndexType globalLinearIndex = dfg_->getGlobalLinearIndex(i);
      std::vector<real> globalCoords(dim);
      dfg_->getCoordsGlobal(globalLinearIndex, globalCoords);
      elements[i] = TaskExample::myfunction(globalCoords, 0.0);
    }

    initialized_ = true;
  }
  /* this is were the application code kicks in and all the magic happens.
   * do whatever you have to do, but make sure that your application uses
   * only lcomm or a subset of it as communicator.
   * important: don't forget to set the isFinished flag at the end of the computation.
   */
  void run(CommunicatorType lcomm) {
    assert(initialized_);

    int globalRank = theMPISystem()->getGlobalRank();
    int lrank = theMPISystem()->getLocalRank();

    /* pseudo timestepping to demonstrate the behaviour of your typical
     * time-dependent simulation problem. */
    auto elements = dfg_->getData();

    for (size_t step = stepsTotal_; step < stepsTotal_ + nsteps_; ++step) {
      real time = (step + 1)* dt_;

      for (size_t i = 0; i < dfg_->getNrLocalElements(); ++i) {
        IndexType globalLinearIndex = dfg_->getGlobalLinearIndex(i);
        std::vector<real> globalCoords(this->getDim());
        dfg_->getCoordsGlobal(globalLinearIndex, globalCoords);
        elements[i] = TaskExample::myfunction(globalCoords, time);
      }
    }

    stepsTotal_ += nsteps_;
    this->setFinished(true);
    decideToKill();
    ++combiStep_;
  }

  /* this function evaluates the combination solution on a given full grid.
   * here, a full grid representation of your task's solution has to be created
   * on the process of lcomm with the rank r.
   * typically this would require gathering your (in whatever way) distributed
   * solution on one process and then converting it to the full grid representation.
   * the DistributedFullGrid class offers a convenient function to do this.
   */
  void getFullGrid(FullGrid<CombiDataType>& fg, RankType r,
                   CommunicatorType lcomm, int n = 0) {
    assert(fg.getLevels() == dfg_->getLevels());
    dfg_->gatherFullGrid(fg, r);
  }

  DistributedFullGrid<CombiDataType>& getDistributedFullGrid(int n) override {
    return *dfg_;
  }

  const DistributedFullGrid<CombiDataType>& getDistributedFullGrid(int n) const override{
    return *dfg_;
  }

  static real myfunction(std::vector<real>& coords, real t) {
    real u = std::cos(M_PI * t);

    for ( size_t d = 0; d < coords.size(); ++d )
      u *= std::cos( 2.0 * M_PI * coords[d] );

    return u;

    /*
    double res = 1.0;
    for (size_t i = 0; i < coords.size(); ++i) {
      res *= -4.0 * coords[i] * (coords[i] - 1);
    }


    return res;
    */
  }

 inline void setStepsTotal( size_t stepsTotal );

 inline void setZero(){

 }

  ~TaskExample() {
    if (dfg_ != NULL)
      delete dfg_;
  }

 protected:
  /* if there are local variables that have to be initialized at construction
   * you have to do it here. the worker processes will create the task using
   * this constructor before overwriting the variables that are set by the
   * manager. here we need to set the initialized variable to make sure it is
   * set to false. */
  TaskExample() :
    initialized_(false), combiStep_(0), dfg_(NULL) {
  }

 private:
  friend class boost::serialization::access;

  // new variables that are set by manager. need to be added to serialize
  real dt_;
  size_t nsteps_;
  size_t stepsTotal_;
  std::vector<int> p_;

  // pure local variables that exist only on the worker processes
  bool initialized_;
  size_t combiStep_;



  OwningDistributedFullGrid<CombiDataType>* dfg_;

  /**
   * The serialize function has to be extended by the new member variables.
   * However this concerns only member variables that need to be exchanged
   * between manager and workers. We do not need to add "local" member variables
   * that are only needed on either manager or worker processes.
   * For serialization of the parent class members, the class must be
   * registered with the BOOST_CLASS_EXPORT macro.
   */
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // handles serialization of base class
    ar& boost::serialization::base_object<Task>(*this);

    // add our new variables
    ar& dt_;
    ar& nsteps_;
    ar& stepsTotal_;
    ar& p_;
  }

  void decideToKill(){ //toDo check if combiStep should be included in task and sent to process groups in case of reassignment
    using namespace std::chrono;

    int globalRank = theMPISystem()->getGlobalRank();
    // MPI_Comm_rank(lcomm, &lrank);
    // MPI_Comm_rank(MPI_COMM_WORLD, &globalRank);
    //theStatsContainer()->setTimerStop("computeIterationRank" + std::to_string(globalRank));
    //duration<real> dur = high_resolution_clock::now() - startTimeIteration_;
    //real t_iter = dur.count();
    //std::cout << "Current iteration took " << t_iter << "\n";

    //theStatsContainer()->setTimerStart("computeIterationRank" + std::to_string(globalRank));


    //check if killing necessary
    //std::cout << "failNow result " << failNow(globalRank) << " at rank: " << globalRank <<" at step " << combiStep_ << "\n" ;
    //real t = dt_ * nsteps_ * combiStep_;
    if (combiStep_ != 0 && faultCriterion_->failNow(combiStep_, -1.0, globalRank)){
          std::cout<<"Rank "<< globalRank <<" failed at iteration "<<combiStep_<<std::endl;
          StatusType status=PROCESS_GROUP_FAIL;
          MASTER_EXCLUSIVE_SECTION{
            simft::Sim_FT_MPI_Send( &status, 1, MPI_INT,  theMPISystem()->getManagerRank(), TRANSFER_STATUS_TAG,
                              theMPISystem()->getGlobalCommFT() );
          }
          theMPISystem()->sendFailedSignal();
          simft::Sim_FT_kill_me();
    }
  }

};





inline void TaskExample::setStepsTotal( size_t stepsTotal ) {
  stepsTotal_ = stepsTotal;
}

} // namespace combigrid

#endif /* TASKEXAMPLE_HPP_ */
