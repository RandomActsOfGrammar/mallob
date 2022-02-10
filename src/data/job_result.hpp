
#ifndef DOMPASCH_MALLOB_JOB_RESULT_H
#define DOMPASCH_MALLOB_JOB_RESULT_H

#include <memory>
#include <vector>
#include <cstring>

#include "serializable.hpp"
#include "util/assert.hpp"

struct JobResult : public Serializable {

    int id = 0;
    int revision;
    int result;

private:
    std::vector<int> solution;
    std::vector<uint8_t> packedData;

public:
    JobResult() {}
    JobResult(int id, int result, std::vector<int> solution) : id(id), result(result), solution(solution) {}
    JobResult(std::vector<uint8_t>&& packedData);

    int getTransferSize() const {return sizeof(int)*3 + sizeof(int)*solution.size();}

    JobResult& deserialize(const std::vector<uint8_t>& packed) override;

    std::vector<uint8_t> serialize() const override;

    void setSolution(std::vector<int>&& solution);

    size_t getSolutionSize() const;
    inline int getSolution(size_t pos) const {
        assert(pos < getSolutionSize());
        if (!packedData.empty()) {
            return *(
                (int*) (packedData.data() + (3+pos)*sizeof(int))
            );
        }
        return solution[pos];
    }
    std::vector<int> extractSolution();
};

#endif