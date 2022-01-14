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

        fieldnames.lockExclusive()->clear();
        real_data.lockExclusive()->clear();
        complex_data.lockExclusive()->clear();
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
        fieldnames.lockExclusive()->clear();
        real_data.lockExclusive()->clear();
        complex_data.lockExclusive()->clear();
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

        fieldnames.lockExclusive()->clear();
        real_data.lockExclusive()->clear();
        complex_data.lockExclusive()->clear();
        ngSpice_Command((char*)"bg_op");
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
        fieldnames.lockExclusive()->clear();
        real_data.lockExclusive()->clear();
        complex_data.lockExclusive()->clear();
        ngSpice_Command(buf);
        *is_running.lockExclusive() = true;
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }

    // Callback functions
    static int cbSendChar( char* what, int id, void* user ) {
        std::cout << "SendChar: " << what << std::endl;
        return 0;
    }
    static int cbSendStat( char* what, int id, void* user ) {
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
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        auto fieldnames = cmd->fieldnames.lockExclusive();
        auto real_data = cmd->real_data.lockExclusive();
        auto complex_data = cmd->complex_data.lockExclusive();
        std::cout << via->name << std::endl;
        for(int i=0; i<via->veccount; i++) {
            fieldnames->push_back(via->vecs[i]->vecname);
            // std::cout << via->vecs[i]->vecname << std::endl;
        }
        real_data->resize(via->veccount);
        complex_data->resize(via->veccount);
        return 0;
    }
    static int cbSendData(pvecvaluesall vva, int len, int id, void* user) {
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        auto real_data = cmd->real_data.lockExclusive();
        auto complex_data = cmd->complex_data.lockExclusive();
        for(int i=0; i<vva->veccount; i++) {
            auto vecsa = vva->vecsa[i];
            // std::cout << vva->vecsa[i]->name << "(" << is_complex << "): " << vva->vecsa[i]->creal << " " << vva->vecsa[i]->cimag << std::endl;
            if(vecsa->is_scale) {
                *cmd->scale.lockExclusive() = i;
            }
            // ngspice has bool all messed up
            if(vecsa->is_complex) {
                (*complex_data)[i].push_back(std::complex(vecsa->creal, vecsa->cimag));
            } else {
                (*real_data)[i].push_back(vecsa->creal);
            }
        }
        return 0;
    }

    std::string name;

    kj::MutexGuarded<std::vector<std::string>> fieldnames;
    kj::MutexGuarded<std::vector<std::vector<double>>> real_data;
    kj::MutexGuarded<std::vector<std::vector<std::complex<double>>>> complex_data;
    kj::MutexGuarded<bool> is_running;
    kj::MutexGuarded<kj::Maybe<unsigned int>> scale;
};

kj::Promise<void> ResultImpl::read(ReadContext context)
{
    auto fieldnames = cmd->fieldnames.lockExclusive();
    auto real_data = cmd->real_data.lockExclusive();
    auto complex_data = cmd->complex_data.lockExclusive();

    auto res = context.getResults();
    KJ_IF_MAYBE(scale, *cmd->scale.lockExclusive()) {
        res.setScale((*fieldnames)[*scale].c_str());
    }
    res.setMore(*cmd->is_running.lockExclusive());
    auto datlist = res.initData(fieldnames->size());
    for (size_t i = 0; i < fieldnames->size(); i++)
    {
        datlist[i].setName((*fieldnames)[i]);
        auto dat = datlist[i].getData();
        if (!(*complex_data)[i].empty())
        {
            auto simdat = (*complex_data)[i];
            auto list = dat.initComplex(simdat.size());
            for (size_t j = 0; j < simdat.size(); j++)
            {
                list[j].setReal(simdat[j].real());
                list[j].setImag(simdat[j].imag());
            }
        }
        else if (!(*real_data)[i].empty())
        {
            auto simdat = (*real_data)[i];
            auto list = dat.initReal(simdat.size());
            for (size_t j = 0; j < simdat.size(); j++)
            {
                list.set(j, simdat[j]);
            }
        } // else no data apparently
        (*real_data)[i].clear();
        (*complex_data)[i].clear();
    }
    return kj::READY_NOW;
}

typedef Sim::NgspiceCommands SimCommands;
typedef NgspiceCommandsImpl SimCommandsImpl;
