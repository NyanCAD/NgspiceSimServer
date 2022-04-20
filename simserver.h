#include "api/Simulator.capnp.h"
#include <kj/debug.h>
#include <kj/filesystem.h>
#include <kj/exception.h>
#include <kj/async-io.h>
#include <capnp/ez-rpc.h>
#include <capnp/message.h>
#include <iostream>
#include <vector>
#include <complex>
#include <sstream>
#include <ngspice/sharedspice.h>

class NgspiceCommandsImpl;

class ResultImpl final : public Sim::Result::Server
{
public:
    ResultImpl(NgspiceCommandsImpl* cmd) : cmd(cmd) {}

    kj::Promise<void> read(ReadContext context);

    NgspiceCommandsImpl* cmd;
};

struct NgVectors {
    std::string name;
    std::vector<std::string> fieldnames;
    std::vector<std::vector<double>> real_data;
    std::vector<std::vector<std::complex<double>>> complex_data;
    kj::Maybe<unsigned int> scale;
};

class NgspiceCommandsImpl final : public Sim::NgspiceCommands::Server
{
public:
    NgspiceCommandsImpl(std::string name) : name(name) {
        ngSpice_Init(&cbSendChar, &cbSendStat, &cbControlledExit, &cbSendData, &cbSendInitData, &cbBGThreadRunning, this);
        ngSpice_Command((char*)("source " + name).c_str());
    }

    ~NgspiceCommandsImpl() {
        // need to halt the thread before destroying ourselves
        // bg thread runs callbacks on us
        if(ngSpice_running()) {
            ngSpice_Command((char*)"bg_halt");
            ngSpice_Command((char*)"quit");
        }
    }

    kj::Promise<void> run(RunContext context)
    {
        std::ostringstream ss;
        ss << "save";
        for (auto v : context.getParams().getVectors()) {
            ss << " " << v.cStr();
        }
        const char* savecmd = ss.str().c_str();
        // std::cout << savecmd << std::endl;
        // sim->m_ngSpice_Command("save none");
        ngSpice_Command((char*)savecmd);

        vectors.lockExclusive()->clear();
        ngSpice_Command((char*)"bg_run");
        *is_running.lockExclusive() = true;
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }

    kj::Promise<void> tran(TranContext context)
    {
        auto params = context.getParams();
        std::ostringstream ss;
        ss << "save";
        for (auto v : params.getVectors()) {
            ss << " " << v.cStr();
        }
        const char* savecmd = ss.str().c_str();
        // std::cout << savecmd << std::endl;
        // sim->m_ngSpice_Command("save none");
        ngSpice_Command((char*)savecmd);

        char buf[256];
        snprintf(buf, 256, "bg_tran %f %f %f", params.getStep(), params.getStop(), params.getStart());
        vectors.lockExclusive()->clear();
        ngSpice_Command(buf);
        *is_running.lockExclusive() = true;
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }

    kj::Promise<void> op(OpContext context)
    {
        std::ostringstream ss;
        ss << "save";
        for (auto v : context.getParams().getVectors()) {
            ss << " " << v.cStr();
        }
        const char* savecmd = ss.str().c_str();
        // std::cout << savecmd << std::endl;
        // sim->m_ngSpice_Command("save none");
        ngSpice_Command((char*)savecmd);

        vectors.lockExclusive()->clear();
        ngSpice_Command((char*)"bg_op");
        *is_running.lockExclusive() = true;
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }

    kj::Promise<void> dc(DcContext context)
    {
        auto params = context.getParams();
        std::ostringstream ss;
        ss << "save";
        for (auto v : params.getVectors()) {
            ss << " " << v.cStr();
        }
        const char* savecmd = ss.str().c_str();
        // std::cout << savecmd << std::endl;
        // sim->m_ngSpice_Command("save none");
        ngSpice_Command((char*)savecmd);

        char buf[256];
        snprintf(buf, 256, "bg_dc %s %f %f %f", params.getSrc().cStr(), params.getVstart(), params.getVstop(), params.getVincr());
        std::cout << buf << std::endl;
        vectors.lockExclusive()->clear();
        ngSpice_Command(buf);
        *is_running.lockExclusive() = true;
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }

    kj::Promise<void> ac(AcContext context)
    {
        auto params = context.getParams();
        std::ostringstream ss;
        ss << "save";
        for (auto v : params.getVectors()) {
            ss << " " << v.cStr();
        }
        const char* savecmd = ss.str().c_str();
        // std::cout << savecmd << std::endl;
        // sim->m_ngSpice_Command("save none");
        ngSpice_Command((char*)savecmd);

        const char* mode;
        switch (params.getMode())
        {
        case Sim::AcType::LIN:
            mode = "lin";
            break;
        case Sim::AcType::OCT:
            mode = "oct";
            break;
        case Sim::AcType::DEC:
            mode = "dec";
            break;
        }
        char buf[256];
        snprintf(buf, 256, "bg_ac %s %d %f %f", mode, params.getNum(), params.getFstart(), params.getFstop());
        std::cout << buf << std::endl;
        vectors.lockExclusive()->clear();
        ngSpice_Command(buf);
        *is_running.lockExclusive() = true;
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }

    kj::Promise<void> noise(NoiseContext context)
    {
        auto params = context.getParams();
        std::ostringstream ss;
        ss << "save";
        for (auto v : params.getVectors()) {
            ss << " " << v.cStr();
        }
        const char* savecmd = ss.str().c_str();
        // std::cout << savecmd << std::endl;
        // sim->m_ngSpice_Command("save none");
        ngSpice_Command((char*)savecmd);

        const char* mode;
        switch (params.getMode())
        {
        case Sim::AcType::LIN:
            mode = "lin";
            break;
        case Sim::AcType::OCT:
            mode = "oct";
            break;
        case Sim::AcType::DEC:
            mode = "dec";
            break;
        }
        char buf[512];
        snprintf(buf, 512, "bg_noise %s %s %s %d %f %f", params.getOutput().cStr(), params.getSrc().cStr(), mode, params.getNum(), params.getFstart(), params.getFstop());
        std::cout << buf << std::endl;
        vectors.lockExclusive()->clear();
        ngSpice_Command(buf);
        *is_running.lockExclusive() = true;
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }


    // Callback functions
    static int cbSendChar( char* what, int id, void* user ) {
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        auto buf = cmd->outputbuf.lockExclusive();
        buf->append(what);
        buf->push_back('\n');
        std::cout << "SendChar: " << what << std::endl;
        return 0;
    }
    static int cbSendStat( char* what, int id, void* user ) {
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        auto buf = cmd->outputbuf.lockExclusive();
        buf->append(what);
        buf->push_back('\n');
        std::cout << "SendStat: " << what << std::endl;
        return 0;
    }
    static int cbBGThreadRunning( bool halted, int id, void* user ) {
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        *cmd->is_running.lockExclusive() = !halted;
        if (halted) {
            std::cout << "BGThreadRunning: not running" << std::endl;
        } else {
            std::cout << "BGThreadRunning: running" << std::endl;
        }
        return 0;
    }
    static int cbControlledExit( int status, bool immediate, bool exit_upon_quit, int id, void* user ) {
        exit(1);
        return 0;
    }
    static int cbSendInitData(pvecinfoall via, int id, void* user) {
        std::cout << "init data\n";
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        NgVectors vec;
        vec.name = via->name;
        std::cout << via->name << std::endl;
        for(int i=0; i<via->veccount; i++) {
            vec.fieldnames.push_back(via->vecs[i]->vecname);
            std::cout << via->vecs[i]->vecname << std::endl;
        }
        vec.real_data.resize(via->veccount);
        vec.complex_data.resize(via->veccount);
        cmd->vectors.lockExclusive()->push_back(vec);
        return 0;
    }
    static int cbSendData(pvecvaluesall vva, int len, int id, void* user) {
        // std::cout << "send data\n";
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        NgVectors &vec = cmd->vectors.lockExclusive()->back();
        for(int i=0; i<vva->veccount; i++) {
            auto vecsa = vva->vecsa[i];
            // std::cout << vva->vecsa[i]->name << "(" << vecsa->is_complex << "): " << vva->vecsa[i]->creal << " " << vva->vecsa[i]->cimag << std::endl;
            if(vecsa->is_scale) {
                vec.scale = i;
            }
            // ngspice has bool all messed up
            if(vecsa->is_complex) {
                vec.complex_data[i].push_back(std::complex(vecsa->creal, vecsa->cimag));
            } else {
                vec.real_data[i].push_back(vecsa->creal);
            }
        }
        return 0;
    }

    std::string name;

    kj::MutexGuarded<std::vector<NgVectors>> vectors;
    kj::MutexGuarded<std::string> outputbuf;
    kj::MutexGuarded<bool> is_running;
};

kj::Promise<void> ResultImpl::read(ReadContext context)
{
    auto res = context.getResults();
    res.setMore(*cmd->is_running.lockExclusive());
    {
        auto outputbuf = cmd->outputbuf.lockExclusive();
        res.setStdout(*outputbuf);
        outputbuf->clear();
    }
    auto vecs = cmd->vectors.lockExclusive();
    auto dat = res.initData(vecs->size());
    for(size_t h = 0; h< vecs->size(); h++) {
        NgVectors &vec = (*vecs)[h];
        auto res = dat[h];
        KJ_IF_MAYBE(scale, vec.scale) {
            res.setScale(vec.fieldnames[*scale]);
        }
        res.setName(vec.name);
        auto datlist = res.initData(vec.fieldnames.size());
        for (size_t i = 0; i < vec.fieldnames.size(); i++)
        {
            datlist[i].setName(vec.fieldnames[i]);
            auto dat = datlist[i].getData();
            if (!vec.complex_data[i].empty())
            {
                auto simdat = vec.complex_data[i];
                auto list = dat.initComplex(simdat.size());
                for (size_t j = 0; j < simdat.size(); j++)
                {
                    list[j].setReal(simdat[j].real());
                    list[j].setImag(simdat[j].imag());
                }
            }
            else if (!vec.real_data[i].empty())
            {
                auto simdat = vec.real_data[i];
                auto list = dat.initReal(simdat.size());
                for (size_t j = 0; j < simdat.size(); j++)
                {
                    list.set(j, simdat[j]);
                }
            } // else no data apparently
            vec.real_data[i].clear();
            vec.complex_data[i].clear();
        }
    }
    return kj::READY_NOW;
}

typedef Sim::NgspiceCommands SimCommands;
typedef NgspiceCommandsImpl SimCommandsImpl;
