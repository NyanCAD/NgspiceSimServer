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
#include <dlfcn.h>

// Ngspice defines bool as int
typedef struct cppvecvalues {
    char* name;        /* name of a specific vector */
    double creal;      /* actual data value */
    double cimag;      /* actual data value */
    int is_scale;     /* if 'name' is the scale vector */
    int is_complex;   /* if the data are complex numbers */
} cppvecvalues, *cpppvecvalues;

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

    kj::Promise<void> read(ReadContext context);

    NgspiceCommandsImpl* cmd;
};


class NgspiceCommandsImpl final : public Sim::NgspiceCommands::Server
{
public:
    NgspiceCommandsImpl(std::string name) : name(name) {
        sim = kj::heap<NgSpice>(&cbSendChar, &cbSendStat, &cbControlledExit, &cbSendData, &cbSendInitData, &cbBGThreadRunning, this);
        sim->m_ngSpice_Command(("source " + name).c_str());
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
        sim->m_ngSpice_Command(savecmd);

        fieldnames.lockExclusive()->clear();
        real_data.lockExclusive()->clear();
        complex_data.lockExclusive()->clear();
        sim->m_ngSpice_Command("bg_run");
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
        sim->m_ngSpice_Command(savecmd);

        char buf[256];
        snprintf(buf, 256, "bg_tran %f %f %f", params.getStep(), params.getStop(), params.getStart());
        fieldnames.lockExclusive()->clear();
        real_data.lockExclusive()->clear();
        complex_data.lockExclusive()->clear();
        sim->m_ngSpice_Command(buf);
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
        sim->m_ngSpice_Command(savecmd);

        fieldnames.lockExclusive()->clear();
        real_data.lockExclusive()->clear();
        complex_data.lockExclusive()->clear();
        sim->m_ngSpice_Command("bg_op");
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
        sim->m_ngSpice_Command(savecmd);

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
        sim->m_ngSpice_Command(buf);
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
            auto vecsa = reinterpret_cast<cpppvecvalues>(vva->vecsa[i]);
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
    kj::Own<NgSpice> sim;

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
    std::string listen = "*:5923";
    if (argc == 2) {
        listen = argv[1];
    }
    capnp::EzRpcServer server(kj::heap<SimulatorImpl>(dir), listen);

    auto &waitScope = server.getWaitScope();
    server.getIoProvider().getTimer();
    uint port = server.getPort().wait(waitScope);
    std::cout << "Listening on port " << port << "..." << std::endl;

    // Run forever, accepting connections and handling requests.
    kj::NEVER_DONE.wait(waitScope);
}
