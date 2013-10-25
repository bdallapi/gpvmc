#ifdef USEMPI
#include <mpi.h>
#endif
#include <ctime>
#include <vector>
#include "MetroMC.h"
#include "SpinState.h"
#include "StagFluxTransExciton.h"
#include "FullSpaceStepper.h"
#include "ProjHeis.h"
#include "Amplitude.h"
#include "RanGen.h"
#include "FileManager.h"
#include "StaggMagnJastrow.h"
#include "StagJastrow.h"
#include "ArgParse.h"
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <hdf5.h>
#include <hdf5_hl.h>

using namespace std;

int main(int argc, char* argv[])
{
    // Initialize
#ifdef USEMPI
    MPI_Init(&argc,&argv);
#endif
    int comm_size(1);
    int comm_rank(0);
#ifdef USEMPI
    MPI_Comm_size(MPI_COMM_WORLD,&comm_size);
    MPI_Comm_rank(MPI_COMM_WORLD,&comm_rank);
#endif
    signal(SIGTERM,FileManager::EmergencyClose);
    int seed=time(NULL)+100*comm_rank;
    RanGen::srand(seed);
    Timer::tic("main");

    // calculation parameters:
    map<string,bool> bomap;
    map<string,size_t> simap;
    map<string,int> inmap;
    map<string,double> domap;
    map<string,string> stmap;
    simap["L"]=8;
    simap["samples"]=10;
    simap["samples_saves"]=1;
    simap["samples_saves_stat"]=100;
    simap["qx"]=0;
    simap["qy"]=0;
    inmap["prefix"]=-1;
    inmap["therm"]=100;
    inmap["verbose"]=1;
    domap["phi"]=0.085;
    domap["neel"]=0.0;
    domap["jastrow"]=0.0;
    domap["phase_shift_x"]=1.0;
    domap["phase_shift_y"]=1.0;
    domap["jr"]=0.0;
    domap["cutoff"]=0.0;
    bomap["jas_onebodystag"]=true;
    bomap["jas_twobodystag"]=false;
    stmap["dir"]=".";
    stmap["spinstate"]="";
    ArgParse arg(argc,argv);
    arg.SetupParams(bomap,simap,inmap,domap,stmap);
    // Setup calculation parameters
    FileManager fm(stmap["dir"],inmap["prefix"]);
    fm.Verbose()=inmap["verbose"];
    fm.MonitorTotal()=simap["samples"]*simap["samples_saves"];
    fm.StatPerSample()=simap["samples_saves"];
    if(comm_rank==0) std::cout<<simap["qx"]<<" "<<simap["qy"]<<std::endl;
    domap["phi"]*=M_PI;
    for(map<string,bool>::iterator it=bomap.begin();it!=bomap.end();++it)
        fm.FileAttribute(it->first,it->second);
    for(map<string,size_t>::iterator it=simap.begin();it!=simap.end();++it)
        fm.FileAttribute(it->first,it->second);
    for(map<string,int>::iterator it=inmap.begin();it!=inmap.end();++it)
        fm.FileAttribute(it->first,it->second);
    for(map<string,double>::iterator it=domap.begin();it!=domap.end();++it)
        fm.FileAttribute(it->first,it->second);
    for(map<string,string>::iterator it=stmap.begin();it!=stmap.end();++it)
        fm.FileAttribute(it->first,it->second);

#ifdef USEMPI
    if(comm_rank){
#endif
        // Setup calculation
        double rej=0;
        size_t L=simap["L"], Q[2];
        double neel=domap["neel"], phi=domap["phi"], cutoff=domap["cutoff"];
        double phase_shift[2];
        phase_shift[0]=domap["phase_shift_x"];
        phase_shift[1]=domap["phase_shift_y"];
        Q[0]=simap["qx"];
        Q[1]=simap["qy"];
        SpinState sp(L,L*L/2+1,L*L/2-1);
        if(stmap["spinstate"]!=""){
            char* ist=new char[L*L];
#ifdef USEMPI
            if(comm_rank==1){
#endif
                hid_t ifile=H5Fopen(stmap["spinstate"].c_str(),H5F_ACC_RDONLY,H5P_DEFAULT);
#ifdef USEMPI
                H5LTread_dataset_char(ifile,"/rank-1",ist);
#else
                H5LTread_dataset_char(ifile,"/rank-0",ist);
#endif
                sp.Init(ist);
#ifdef USEMPI
                for(int r=2;r<comm_size;++r){
                    ostringstream rst;
                    rst<<"/rank-"<<r;
                    H5LTread_dataset_char(ifile,rst.str().c_str(),ist);
                    MPI_Send(ist,L*L,MPI_CHAR,r,0,MPI_COMM_WORLD);
                }
                H5Fclose(ifile);
            } else {
                MPI_Recv(ist,L*L,MPI_CHAR,1,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
                sp.Init(ist);
            }
#endif
            delete [] ist;
        } else {
            sp.Init();
        }
        double bdwd=2*sqrt(1+neel*neel);
        StagFluxTransExciton wav(L,L,phi,neel,phase_shift,Q,cutoff*bdwd);
        wav.save(&fm);
        Jastrow* jas=0;
        if(domap["jastrow"]!=0){
            if(bomap["jas_onebodystag"])
                jas=new StaggMagnJastrow(&sp,domap["jastrow"]);
            else if(bomap["jas_twobodystag"])
                jas=new StagJastrow(&sp,domap["jastrow"]);
        }
        Amplitude amp(&sp,&wav);
        if(stmap["spinstate"]==""){
            while(amp.Amp()==0.0){
                sp.Init();
                amp.Init();
            }
        } else {
            amp.Init();
        }
        FullSpaceStepper step(&amp);
        ProjHeis seen(&step,&fm,domap["jr"]);
        MetroMC varmc(&step,&fm);
        varmc.AddQuantity(&seen);

        // Start calculation: thermalize
        cout<<"rank "<<comm_rank<<": thermalize"<<endl;
        if(simap["therm"]){
            Timer::tic("main/thermalize");
            varmc.Walk(int(simap["therm"]*L*L),0);
            Timer::toc("main/thermalize");
        }
        fm.MonitorTotal()=simap["samples"]*simap["samples_saves"];         
        // Calculation
        for(size_t sample=0;sample<simap["samples"];++sample){
            for(size_t m=0;m<simap["samples_saves"];++m){
                fm.MonitorCompletion()=double(sample*simap["samples_saves"]+m)/(simap["samples"]*simap["samples_saves"]);
                Timer::tic("main/ranwalk");
                varmc.Walk(L*L*simap["samples_saves_stat"],L*L);
                Timer::toc("main/ranwalk");
                rej=varmc.Rejection();
                seen.save();
                fm.DataAttribute("rej",rej);
                fm.DataAttribute("time",Timer::timer("randwalk"));
                fm.DataAttribute("statistics",(m+1)*simap["samples_saves_stat"]);
#ifdef USEMPI
                int mess(fm.message_save);
                //cout<<"rank "<<comm_rank<<": sends message_save"<<endl;
                MPI_Send(&mess,1,MPI_INT,0,fm.message_comm,MPI_COMM_WORLD);
#endif
                fm.Write();
            }
        }
#ifdef USEMPI
        int mess=fm.message_loop, stop=1;
        //cout<<"rank "<<comm_rank<<": sends message_loop"<<endl;
        MPI_Send(&mess,1,MPI_INT,0,fm.message_comm,MPI_COMM_WORLD);
        MPI_Send(&stop,1,MPI_INT,0,fm.message_loop,MPI_COMM_WORLD);
#endif
        sp.save(&fm);
        if(jas) delete jas;
#ifdef USEMPI
    } else {
        fm.MainLoop();
    }
#endif
    // Output
    Timer::toc("main");
    if(comm_rank==0){
        for(int r=0;r<comm_size;++r){
            std::cout<<"############################################"<<std::endl;
            std::cout<<"rank="<<r<<endl;
            if(r==0)
                std::cout<<Timer::report();
#ifdef USEMPI
            else {
                int len(0);
                MPI_Recv(&len,1,MPI_INT,r,1,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
                char *mes=new char[len+1];
                MPI_Recv(mes,len+1,MPI_CHAR,r,1,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
                cout<<mes<<endl;
                delete [] mes;
            }
#endif
            std::cout<<"############################################"<<std::endl;
        }
        std::cout<<"output prefix="<<fm.Prefix()<<std::endl;
    } else {
#ifdef USEMPI
        string rep=Timer::report();
        int len=rep.size();
        MPI_Send(&len,1,MPI_INT,0,1,MPI_COMM_WORLD);
        char* mes=new char[len+1];
        memcpy(mes,rep.c_str(),(len+1)*sizeof(char));
        MPI_Send(mes,len+1,MPI_CHAR,0,1,MPI_COMM_WORLD);
        delete [] mes;
#endif
    }
#ifdef USEMPI
    MPI_Finalize();
#endif
    return 0;
}

