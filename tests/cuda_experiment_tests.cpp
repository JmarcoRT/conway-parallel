#include "app/experiment_runner.hpp"
#include "app/scaling_study.hpp"
#include "app/experiment_csv.hpp"
#include "app/experiment_form.hpp"
#include "app/cuda_info.hpp"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
using namespace conway;
using namespace conway::app;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f) {bool failed=false;try {f();}catch(const std::exception&) {failed=true;}require(failed,"expected explicit rejection");}
int main(int argc,char** argv) {
    try {
        const bool unavailable=argc>1 && std::string(argv[1])=="--unavailable";
        InputData input{17,17,std::vector<std::uint8_t>(17*17)};
        for(std::size_t i=0;i<input.cells.size();++i)input.cells[i]=(i*13+i/17)%7<3;
        ExperimentConfig config;config.generations=4;config.warmups=1;config.repetitions=3;
        config.participants={{Engine::Sequential,{},{}},{Engine::OpenMP,2,{}},{Engine::CUDA,{},{},core::kCudaBlock16x16}};
        auto draft=standardExperimentForm(false,2,true);draft.cuda=true;
        require(validateExperimentForm(draft,false,&input).config.has_value(),"CUDA form rejected");
        draft.values[static_cast<std::size_t>(ExperimentField::CudaBlocks)]="7x7";
        require(!validateExperimentForm(draft,false,&input).config,"invalid block accepted");
        auto available=inspectCuda(17,17);
        if(!available.available) {
            rejects([&]{runExperiment(input,config);});
            if(!unavailable) {
                if(available.diagnostic.find("GPU ausente")!=std::string::npos || available.diagnostic.find("build sin CUDA")!=std::string::npos)return 77;
                throw std::runtime_error(available.diagnostic);
            }
            std::cout<<"PASS CUDA unavailable is explicit without fallback\n";return 0;
        }
        require(!unavailable,"hidden GPU unexpectedly available");
        auto result=runExperiment(input,config);require(result.study.has_value(),"missing generalized result");
        auto& study=*result.study;validateStudy(study);
        require(study.complete && study.cleanupConfirmed && study.samples.size()==9 && study.completedWarmupRuns==3,"incomplete study/warmups");
        for(const auto& sample:study.samples) {
            require(sample.valid && sample.equivalent,"invalid/non-equivalent sample");
            const auto& c=study.configurations[sample.configurationIndex];
            if(c.engine==Engine::CUDA)require(sample.cuda && !sample.minEffectiveThreads && !sample.observedProcesses && sample.cuda->totalMs>=sample.elapsedMs,"CUDA sample uses CPU/MPI resources or omits costs");
        }
        require(study.summaries[2].speedup.has_value() && !study.summaries[2].efficiency,"CUDA speedup/efficiency semantics");
        auto synthetic=study;for(auto& sample:synthetic.samples) {sample.elapsedMs=sample.configurationIndex==0?10:5;if(sample.cuda)sample.cuda->totalMs=100;}
        synthetic.summaries=summarizeStudy(synthetic);validateStudy(synthetic);require(synthetic.summaries[2].speedup==2 && !synthetic.summaries[2].efficiency,"synthetic CUDA metrics");
        auto changedGrid=synthetic;changedGrid.samples[2].cuda->gridX++;
        rejects([&]{validateStudy(changedGrid);});
        auto malformed=synthetic;malformed.samples[2].minEffectiveThreads=256;malformed.samples[2].valid=true;
        rejects([&]{validateStudy(malformed);});
        std::ostringstream samples,summary,metadata;const auto context=captureExperimentContext({"cuda-test"});
        writeExperimentCsv(result,context,samples,summary,metadata);
        require(samples.str().find("cuda_resident_batch_wall")!=std::string::npos && samples.str().find("cuda_device_ms")!=std::string::npos,"CSV CUDA columns/scope missing");
        require(summary.str().find("not_applicable_cuda")!=std::string::npos && metadata.str().find("conway-experimental-v3")!=std::string::npos,"CSV CUDA version/efficiency missing");
        ScalingConfig scaling;scaling.parallelEngine=Engine::CUDA;scaling.generations=3;scaling.warmups=0;scaling.repetitions=2;
        auto scaled=runScalingStudy(input,scaling);require(scaled.study && scaled.study->configurations.size()==4,"missing block study");validateStudy(*scaled.study);
        for(const auto& s:scaled.study->summaries)require(s.valid && !s.efficiency,"invalid block study or invented efficiency");
        samples.str("");summary.str("");metadata.str("");writeExperimentCsv(scaled,context,samples,summary,metadata);
        if(const auto* directory=std::getenv("CONWAY_CUDA_EXPORT_DIR")) {
            exportExperimentCsv(result,context,std::filesystem::path(directory)/"comparison");
            exportExperimentCsv(scaled,context,std::filesystem::path(directory)/"blocks");
        }
        scaling.cudaBlocks={core::kCudaBlock8x8,core::kCudaBlock8x8};rejects([&]{runScalingStudy(input,scaling);});
        config.participants.erase(config.participants.begin());auto noReference=runExperiment(input,config);
        for(const auto& s:noReference.study->summaries)require(!s.speedup && !s.efficiency,"metrics without sequential participant");
        config.maxMemoryBytes=1;rejects([&]{runExperiment(input,config);});
        InputData larger{1024,513,std::vector<std::uint8_t>(1024*513)};
        for(std::size_t i=0;i<larger.cells.size();++i)larger.cells[i]=(i*17+i/1024)%7<3;
        ExperimentConfig campaign;campaign.generations=20;campaign.warmups=1;campaign.repetitions=3;
        campaign.participants={{Engine::Sequential,{},{}},{Engine::OpenMP,2,{}},{Engine::CUDA,{},{},core::kCudaBlock16x16}};
        auto substantial=runExperiment(larger,campaign);validateStudy(*substantial.study);
        require(substantial.study->complete && std::all_of(substantial.study->summaries.begin(),substantial.study->summaries.end(),[](const auto& s){return s.valid && s.equivalent;}),"larger CUDA campaign failed equivalence");
        if(argc>2) {
            config.maxMemoryBytes=256u*1024u*1024u;config.participants.insert(config.participants.begin(),{Engine::Sequential,{},{}});
            config.participants.push_back({Engine::MPI,{},2});config.mpiOptions.launcher=argv[1];config.mpiOptions.worker=argv[2];config.mpiOptions.launcherArguments={"--oversubscribe"};
            auto all=runExperiment(input,config);validateStudy(*all.study);require(all.study->complete,"four-engine study incomplete");
        }
        std::cout<<"PASS CUDA comparison, warmups, rotation, metrics, CSV, block study and unavailable behavior\n";
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
