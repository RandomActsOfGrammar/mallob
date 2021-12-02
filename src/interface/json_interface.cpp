
#include <string>

#include "json_interface.hpp"

#include "util/sys/terminator.hpp"
#include "util/params.hpp"
#include "util/random.hpp"
#include "util/sys/time_period.hpp"
#include "app/sat/sat_constants.h"

JsonInterface::Result JsonInterface::handle(const nlohmann::json& json, 
    std::function<void(nlohmann::json&)> feedback) {

    if (Terminator::isTerminating()) return DISCARD;

    std::string userFile, jobName;
    int id;
    float userPrio, arrival, priority;
    JobDescription::Application appl;

    {
        auto lock = _job_map_mutex.getLock();
        
        // Check and read essential fields from JSON
        if (!json.contains("user") || !json.contains("name")) {
            _logger.log(V1_WARN, "[WARN] Job file missing essential field(s). Ignoring this file.\n");
            return DISCARD;
        }
        std::string user = json["user"].get<std::string>();
        std::string name = json["name"].get<std::string>();
        jobName = user + "." + name + ".json";
        bool incremental = json.contains("incremental") ? json["incremental"].get<bool>() : false;

        priority = json.contains("priority") ? json["priority"].get<float>() : 1.0f;
        if (_params.jitterJobPriorities()) {
            // Jitter job priority
            priority *= 0.99 + 0.01 * Random::rand();
        }
        appl = JobDescription::Application::DUMMY;
        if (json.contains("application")) {
            auto appStr = json["application"].get<std::string>();
            appl = appStr == "SAT" ? 
                (incremental ? JobDescription::Application::INCREMENTAL_SAT : JobDescription::Application::ONESHOT_SAT)
                : JobDescription::Application::DUMMY;
        }

        if (json.contains("interrupt") && json["interrupt"].get<bool>()) {
            if (!_job_name_to_id_rev.count(jobName)) {
                _logger.log(V1_WARN, "[WARN] Cannot interrupt unknown job \"%s\"\n", jobName.c_str());
                return DISCARD;
            }
            auto [id, rev] = _job_name_to_id_rev.at(jobName);

            // Interrupt a job which is already present
            JobMetadata data;
            data.description = std::shared_ptr<JobDescription>(new JobDescription(id, 0, appl));
            data.interrupt = true;
            _job_callback(std::move(data));
            return ACCEPT;
        }

        arrival = json.contains("arrival") ? std::max(Timer::elapsedSeconds(), json["arrival"].get<float>()) 
            : Timer::elapsedSeconds();

        if (incremental && json.contains("precursor")) {

            // This is a new increment of a former job - assign SAME internal ID
            auto precursorName = json["precursor"].get<std::string>() + ".json";
            if (!_job_name_to_id_rev.count(precursorName)) {
                _logger.log(V1_WARN, "[WARN] Unknown precursor job \"%s\"!\n", precursorName.c_str());
                return DISCARD;
            }
            auto [jobId, rev] = _job_name_to_id_rev[precursorName];
            id = jobId;

            if (json.contains("done") && json["done"].get<bool>()) {

                // Incremental job is notified to be done
                _logger.log(V3_VERB, "Incremental job #%i is done\n", jobId);
                _job_name_to_id_rev.erase(precursorName);
                for (int rev = 0; rev <= _job_id_to_latest_rev[id]; rev++) {
                    _job_id_rev_to_image.erase(std::pair<int, int>(id, rev));
                }
                _job_id_to_latest_rev.erase(id);

                // Notify client that this incremental job is done
                JobMetadata data;
                data.description = std::shared_ptr<JobDescription>(new JobDescription(id, 0, appl));
                data.done = true;
                _job_callback(std::move(data));
                return ACCEPT_CONCLUDE;

            } else {

                // Job is not done -- add increment to job
                _job_id_to_latest_rev[id] = rev+1;
                _job_name_to_id_rev[jobName] = std::pair<int, int>(id, rev+1);
                JobImage img = JobImage(id, jobName, arrival, feedback);
                img.incremental = true;
                img.baseJson = json;
                _job_id_rev_to_image[std::pair<int, int>(id, rev+1)] = img;
            }

        } else {

            // Create new internal ID for this job
            if (!_job_name_to_id_rev.count(jobName)) 
                _job_name_to_id_rev[jobName] = std::pair<int, int>(_running_id++, 0);
            auto pair = _job_name_to_id_rev[jobName];
            id = pair.first;
            _logger.log(V3_VERB, "Mapping job \"%s\" to internal ID #%i\n", jobName.c_str(), id);

            // Was job already parsed before?
            if (_job_id_rev_to_image.count(std::pair<int, int>(id, 0))) {
                _logger.log(V1_WARN, "[WARN] Modification of a file I already parsed! Ignoring.\n");
                return DISCARD;
            }

            JobImage img(id, jobName, arrival, feedback);
            img.incremental = incremental;
            img.baseJson = json;
            _job_id_rev_to_image[std::pair<int, int>(id, 0)] = std::move(img);
            _job_id_to_latest_rev[id] = 0;
        }
    }

    // Initialize new job
    JobDescription* job = new JobDescription(id, priority, appl);
    job->setRevision(_job_id_to_latest_rev[id]);
    if (json.contains("wallclock-limit")) {
        float limit = TimePeriod(json["wallclock-limit"].get<std::string>()).get(TimePeriod::Unit::SECONDS);
        job->setWallclockLimit(limit);
        _logger.log(V4_VVER, "Job #%i : wallclock time limit %.3f secs\n", id, limit);
    }
    if (json.contains("cpu-limit")) {
        float limit = TimePeriod(json["cpu-limit"].get<std::string>()).get(TimePeriod::Unit::SECONDS);
        job->setCpuLimit(limit);
        _logger.log(V4_VVER, "Job #%i : CPU time limit %.3f CPU secs\n", id, limit);
    }
    if (json.contains("max-demand")) {
        int maxDemand = json["max-demand"].get<int>();
        job->setMaxDemand(maxDemand);
        _logger.log(V4_VVER, "Job #%i : max demand %i\n", id, maxDemand);
    }
    if (json.contains("assumptions")) {
        job->setPreloadedAssumptions(json["assumptions"].get<std::vector<int>>());
    }
    job->setArrival(arrival);
    std::string file = json["file"].get<std::string>();

    
    // Translate dependencies (if any) to internal job IDs
    std::vector<int> idDependencies;
    std::vector<std::string> nameDependencies;
    if (json.contains("dependencies")) 
        nameDependencies = json["dependencies"].get<std::vector<std::string>>();
    const std::string ending = ".json";
    for (auto name : nameDependencies) {
        // Convert to the name with ".json" file ending
        name += ending;
        // If the job is not yet known, assign to it a new ID
        // that will be used by the job later
        auto lock = _job_map_mutex.getLock();
        if (!_job_name_to_id_rev.count(name)) {
            _job_name_to_id_rev[name] = std::pair<int, int>(_running_id++, 0);
            _logger.log(V3_VERB, "Forward mapping job \"%s\" to internal ID #%i\n", name.c_str(), _job_name_to_id_rev[name].first);
        }
        idDependencies.push_back(_job_name_to_id_rev[name].first); // TODO inexact: introduce dependencies for job revisions
    }

    // Callback to client: New job arrival.
    SatReader::ContentMode contentMode = SatReader::ContentMode::ASCII;
    if (json.contains("content-mode") && json["content-mode"] == "raw") {
        contentMode = SatReader::ContentMode::RAW;
    }
    _job_callback(JobMetadata{std::shared_ptr<JobDescription>(job), file, contentMode, idDependencies});

    return ACCEPT;
}

void JsonInterface::handleJobDone(const JobResult& result, const JobDescription::Statistics& stats) {

    if (Terminator::isTerminating()) return;

    auto lock = _job_map_mutex.getLock();
    
    auto& img = _job_id_rev_to_image[std::pair<int, int>(result.id, result.revision)];
    auto& j = img.baseJson;

    // Pack job result into JSON
    j["internal_id"] = result.id;
    j["internal_revision"] = result.revision;
    j["result"] = { 
        { "resultcode", result.result }, 
        { "resultstring", result.result == RESULT_SAT ? "SAT" : result.result == RESULT_UNSAT ? "UNSAT" : "UNKNOWN" }, 
        { "solution", result.solution },
    };
    j["stats"] = {
        { "time", {
            { "parsing", stats.parseTime },
            { "scheduling", stats.schedulingTime },
            { "first_balancing_latency", stats.latencyOf1stVolumeUpdate },
            { "processing", stats.processingTime },
            { "total", Timer::elapsedSeconds() - img.arrivalTime }
        } },
        { "used_wallclock_seconds" , stats.usedWallclockSeconds },
        { "used_cpu_seconds" , stats.usedCpuSeconds }
    };

    // Send back feedback over whichever connection the job arrived
    img.feedback(j);

    if (!img.incremental) {
        _job_name_to_id_rev.erase(img.userQualifiedName);
        _job_id_rev_to_image.erase(std::pair<int, int>(result.id, result.revision));
    }
}
