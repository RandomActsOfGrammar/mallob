
#ifndef DOMPASCH_MALLOB_SAT_CLAUSE_COMMUNICATOR_H
#define DOMPASCH_MALLOB_SAT_CLAUSE_COMMUNICATOR_H

#include "util/params.h"
#include "data/job_transfer.h"
#include "data/job.h"
#include "app/sat_job.h"

#define CLAUSE_EXCHANGE_INITIAL_SIZE 1500
#define CLAUSE_EXCHANGE_MULTIPLIER 3

const int MSG_GATHER_CLAUSES = 417;
const int MSG_DISTRIBUTE_CLAUSES = 418;


class SatClauseCommunicator {

private:
    Parameters& _params;
    SatJob* _job = NULL;

    std::vector<std::vector<int>> _clause_buffers;
    int _num_clause_sources;
    int _job_comm_epoch_of_clause_buffer;
    int _last_shared_job_comm;

public:
    SatClauseCommunicator(Parameters& params, SatJob* job) : _params(params), _job(job), _num_clause_sources(0), 
        _job_comm_epoch_of_clause_buffer(-1), _last_shared_job_comm(-1) {} 
    bool wantsToInitiateCommunication();
    void initiateCommunication();
    void continueCommunication(int source, JobMessage& msg);

private:
    bool canShareCollectedClauses();
    void learnAndDistributeClausesDownwards(std::vector<int>& clauses, int jobCommEpoch);
    /**
     * Get clauses to share from the solvers, in order to propagate it to the parents.
     */
    std::vector<int> collectClausesFromSolvers(int maxSize);
    /**
     * Store clauses from a child node in order to propagate it upwards later.
     */
    void collectClausesFromBelow(std::vector<int>& clauses, int jobCommEpoch);
    /**
     * Returns all clauses that have been added by addClausesFromBelow(·),
     * plus the clauses from an additional call to collectClausesToShare(·).
     */
    std::vector<int> shareCollectedClauses(int jobCommEpoch, int passedLayers);
    /**
     * Give a collection of learned clauses that came from a parent node
     * to the solvers.
     */
    void learnClausesFromAbove(std::vector<int>& clauses, int jobCommEpoch);
    void insertIntoClauseBuffer(std::vector<int>& vec, int jobCommEpoch);

    std::vector<int> merge(const std::vector<std::vector<int>*>& buffers, int maxSize);

};

#endif