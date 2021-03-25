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
#include <ngspice/sharedspice.h>
#include <ngspice/sim.h>
#include <dlfcn.h>

class NgSpice
{
    public:
    NgSpice(SendChar* schar, SendStat* sstat, ControlledExit* exit,
            SendData* data, SendInitData* init,
            BGThreadRunning* running, void* ptr) {
        m_dll = dlopen("libngspice.so", RTLD_LAZY);

        m_ngSpice_Init = (ngSpice_Init) dlsym(m_dll, "ngSpice_Init" );
        m_ngSpice_Circ = (ngSpice_Circ) dlsym(m_dll, "ngSpice_Circ" );
        m_ngSpice_Command = (ngSpice_Command) dlsym(m_dll, "ngSpice_Command" );
        m_ngGet_Vec_Info = (ngGet_Vec_Info) dlsym(m_dll, "ngGet_Vec_Info" );
        m_ngSpice_CurPlot  = (ngSpice_CurPlot) dlsym(m_dll, "ngSpice_CurPlot" );
        m_ngSpice_AllPlots = (ngSpice_AllPlots) dlsym(m_dll, "ngSpice_AllPlots" );
        m_ngSpice_AllVecs = (ngSpice_AllVecs) dlsym(m_dll, "ngSpice_AllVecs" );
        m_ngSpice_Running = (ngSpice_Running) dlsym(m_dll, "ngSpice_running" ); // it is not a typo

        m_ngSpice_Init(schar, sstat, exit, data, init, running, ptr);
    }

    ~NgSpice() {
        // m_ngSpice_Command("quit");
        dlclose(m_dll);
    }

    typedef void ( *ngSpice_Init )( SendChar*, SendStat*, ControlledExit*, SendData*, SendInitData*,
                                    BGThreadRunning*, void* );
    typedef int ( *ngSpice_Circ )( const char** circarray );
    typedef int ( *ngSpice_Command )( const char* command );
    typedef pvector_info ( *ngGet_Vec_Info )( const char* vecname );
    typedef char* ( *ngSpice_CurPlot )( void );
    typedef char** ( *ngSpice_AllPlots )( void );
    typedef char** ( *ngSpice_AllVecs )( const char* plotname );
    typedef bool ( *ngSpice_Running )( void );

    ///< Handle to DLL functions
    ngSpice_Init m_ngSpice_Init;
    ngSpice_Circ m_ngSpice_Circ;
    ngSpice_Command m_ngSpice_Command;
    ngGet_Vec_Info m_ngGet_Vec_Info;
    ngSpice_CurPlot  m_ngSpice_CurPlot;
    ngSpice_AllPlots m_ngSpice_AllPlots;
    ngSpice_AllVecs m_ngSpice_AllVecs;
    ngSpice_Running m_ngSpice_Running;

    void* m_dll;
};

class NgspiceCommandsImpl;

class ResultImpl final : public Sim::Result::Server
{
public:
    ResultImpl(NgspiceCommandsImpl* cmd) : cmd(cmd) {}

    kj::Promise<void> read(ReadContext context)
    {
        return kj::READY_NOW;
    }
    kj::Promise<void> readTime(ReadTimeContext context)
    {

        return kj::READY_NOW;
    }
    kj::Promise<void> readAll(ReadAllContext context);

    NgspiceCommandsImpl* cmd;
};


class NgspiceCommandsImpl final : public Sim::NgspiceCommands::Server
{
public:
    NgspiceCommandsImpl(std::string name) : name(name) {
        sim = kj::heap<NgSpice>(&cbSendChar, &cbSendStat, &cbControlledExit, &cbSendData, &cbSendInitData, &cbBGThreadRunning, this);
        sim->m_ngSpice_Command(("source " + name).c_str());
    }

    kj::Promise<void> run(TranContext context)
    {
        return kj::READY_NOW;
    }

    kj::Promise<void> tran(TranContext context)
    {
        auto params = context.getParams();
        char buf[256];
        snprintf(buf, 256, "bg_tran %f %f %f", params.getStep(), params.getStop(), params.getStart());
        std::cout << "running " << buf << std::endl;
        sim->m_ngSpice_Command(buf);
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }

    kj::Promise<void> op(OpContext context)
    {
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
        auto is_running = cmd->is_running.lockExclusive();
        *is_running = !halted;
        if (halted) {
            std::cout << "BGThreadRunning: not running" << std::endl;
        } else {
            std::cout << "BGThreadRunning: running" << std::endl;
        }
        return 0;
    }
    static int cbControlledExit( int status, bool immediate, bool exit_upon_quit, int id, void* user ) {
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        cmd->sim = nullptr;
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
            std::cout << via->vecs[i]->vecname << std::endl;
        }
        real_data->resize(via->veccount);
        complex_data->resize(via->veccount);
        return 0;
    }
    static int cbSendData(pvecvaluesall vva, int len, int id, void* user) {
        NgspiceCommandsImpl* cmd = reinterpret_cast<NgspiceCommandsImpl*>( user );
        for(int i=0; i<vva->veccount; i++) {
            std::cout << vva->vecsa[i]->name << ": " << vva->vecsa[i]->creal << std::endl;
            if(vva->vecsa[i]->is_complex) {
                auto complex_data = cmd->complex_data.lockExclusive();
                (*complex_data)[i].push_back(std::complex(vva->vecsa[i]->creal, vva->vecsa[i]->cimag));
            } else {
                auto real_data = cmd->real_data.lockExclusive();
                (*real_data)[i].push_back(vva->vecsa[i]->creal);
            }
        }
        return 0;
    }

    std::string name;
    kj::Own<NgSpice> sim;

    kj::MutexGuarded<std::vector<std::string>> fieldnames;
    kj::MutexGuarded<std::vector<std::vector<double>>> real_data;
    kj::MutexGuarded<std::vector<std::vector<std::complex<double>>>> complex_data;
    kj::MutexGuarded<bool> is_running;
};

kj::Promise<void> ResultImpl::readAll(ReadAllContext context)
{
    // TODO use timer, but... how to obtain one?
    return kj::evalLater([this, context]() mutable -> kj::Promise<void> {
        auto is_running = cmd->is_running.lockExclusive();
        if(*is_running) return readAll(context);

        auto fieldnames = cmd->fieldnames.lockExclusive();
        auto real_data = cmd->real_data.lockExclusive();
        auto complex_data = cmd->complex_data.lockExclusive();

        auto res = context.getResults().initData(fieldnames->size());
        for (size_t i = 0; i < fieldnames->size(); i++)
        {
            res[i].setName((*fieldnames)[i]);
            auto dat = res[i].getData();
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
        }
        return kj::READY_NOW;
    });
}

class SimulatorImpl final : public Sim::Ngspice::Server
{
public:
    SimulatorImpl(const kj::Directory &dir) : dir(dir) {}

    kj::Promise<void> loadFiles(LoadFilesContext context) override
    {
        auto files = context.getParams().getFiles();
        for (Sim::File::Reader f : files)
        {
            kj::Path path = kj::Path::parse(f.getName());
            kj::Own<const kj::File> file = dir.openFile(path, kj::WriteMode::CREATE | kj::WriteMode::MODIFY);
            file->truncate(0);
            file->write(0, f.getContents());
        }

        std::string name = files[0].getName();

        auto res = context.getResults();
        auto commands = kj::heap<NgspiceCommandsImpl>(name);
        res.setCommands(kj::mv(commands));
        return kj::READY_NOW;
    }

    const kj::Directory &dir;
};

int main(int argc, const char *argv[])
{
    kj::Own<kj::Filesystem> fs = kj::newDiskFilesystem();
    const kj::Directory &dir = fs->getCurrent();

    // Set up a server.
    capnp::EzRpcServer server(kj::heap<SimulatorImpl>(dir), "*:5923");

    auto &waitScope = server.getWaitScope();
    server.getIoProvider().getTimer();
    uint port = server.getPort().wait(waitScope);
    std::cout << "Listening on port " << port << "..." << std::endl;

    // Run forever, accepting connections and handling requests.
    kj::NEVER_DONE.wait(waitScope);
}
