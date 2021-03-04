
#ifndef DOMSCHREI_MALLOB_JOB_FILE_LISTENER_HPP
#define DOMSCHREI_MALLOB_JOB_FILE_LISTENER_HPP

#include <functional>
#include <atomic>

#include "util/sys/file_watcher.hpp"
#include "util/logger.hpp"
#include "util/robin_hood.hpp"
#include "data/job_description.hpp"
#include "data/job_result.hpp"
#include "data/job_metadata.hpp"
#include "util/json.hpp"
#include "util/sys/timer.hpp"
#include "util/sys/threading.hpp"
#include "util/logger.hpp"

class JobFileAdapter {

public:
    enum Status {NEW, PENDING, DONE};
    struct JobImage {
        int id;
        std::string userQualifiedName;
        std::string originalFileName;
        float arrivalTime;

        JobImage() = default;
        JobImage(int id, const std::string& userQualifiedName, const std::string& originalFileName, float arrivalTime) 
            : id(id), userQualifiedName(userQualifiedName), originalFileName(originalFileName), arrivalTime(arrivalTime) {} 
    };

private:
    const Parameters& _params;
    Logger _logger;

    Mutex _job_map_mutex;
    std::function<void(JobMetadata&&)> _new_job_callback;
    std::atomic_int _running_id;
    
    std::string _base_path;
    FileWatcher _new_jobs_watcher;
    FileWatcher _results_watcher;
    robin_hood::unordered_node_map<std::string, int> _job_name_to_id;
    robin_hood::unordered_node_map<int, JobImage> _job_id_to_image;

public:

    JobFileAdapter(int clientRank, const Parameters& params, Logger&& logger, const std::string& basePath, 
                        std::function<void(JobMetadata&&)> newJobCallback) : 
        _params(params),
        _logger(std::move(logger)),
        _job_map_mutex(),
        _new_job_callback(newJobCallback),
        _running_id(clientRank * 100000 + 1),
        _base_path(basePath),
        _new_jobs_watcher(_base_path + "/new/", (int) (IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE), 
            [&](const FileWatcher::Event& event, Logger& log) {handleNewJob(event, log);},
            _logger, FileWatcher::InitialFilesHandling::TRIGGER_CREATE_EVENT, /*numThreads=*/1),
        _results_watcher(_base_path + "/done/", (int) (IN_DELETE | IN_MOVED_FROM), 
            [&](const FileWatcher::Event& event, Logger& log) {handleJobResultDeleted(event, log);}, 
            _logger) {
        
        // Create directories as necessary
        FileUtils::mkdir(_base_path + "/new/");
        FileUtils::mkdir(_base_path + "/pending/");
        FileUtils::mkdir(_base_path + "/done/");

        _logger.log(V2_INFO, "operational at %s\n", _base_path.c_str());
    }
    ~JobFileAdapter() {
        _logger.log(V2_INFO, "shut down\n");
    }

    // FileWatcher events
    void handleNewJob(const FileWatcher::Event& event, Logger& log);
    void handleJobResultDeleted(const FileWatcher::Event& event, Logger& log);

    // Event when a job is finished
    void handleJobDone(const JobResult& result);

private:
    std::string getJobFilePath(int id, Status status);
    std::string getJobFilePath(const FileWatcher::Event& event, Status status);
    std::string getUserFilePath(const std::string& user);

};

#endif