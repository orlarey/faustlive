//
//  FLEffect.cpp
//  
//
//  Created by Sarah Denoux on 30/05/13.
//  Copyright (c) 2013 __MyCompanyName__. All rights reserved.
//

// An Effect takes care of the compilation of a DSP. Moreover, it is notify in case, the DSP has been modified. 

#include "FLEffect.h"
#include "faust/llvm-dsp.h"
#include "utilities.h"

#ifdef REMOTE
#include "faust/remote-dsp.h"
#endif

//----------CONSTRUCTOR/DESTRUCTOR----------------
//###CONSTRUCTOR
//@param : isEffectRecalled = is Effect created from a session recalling situation
//@param : sourceFile = source of effect
//@param : name = name of the created effect
//@param : isLocal = is it processing on local or remote machine

FLEffect::FLEffect(bool isEffectRecalled, const QString& sourceFile, const QString& name, bool isLocal){
    
    fFactory = NULL;
    fOldFactory = NULL;
    fRemoteFactory = NULL;
    fOldRemoteFactory = NULL;
    fWatcher = NULL;
    fSynchroTimer = NULL;

//File:// is not liked by QFileInfo soooooo it has to be stripped off
//#ifdef _WIN32
//	if(sourceFile.indexOf("file://") == 0)
//		fSource = QString(sourceFile).right(sourceFile.size()-8);
//	else
//#endif
    
    fSource = sourceFile;
    fName = name;
    fForceSynchro = false;
    fRecompilation= false;
    fRecalled = isEffectRecalled;
    
    fIsLocal = isLocal;
    fIpMachineRemote = "127.0.0.1";
    fPortMachineRemote = 0;
}

FLEffect::~FLEffect(){
    reset();
}

//In case of lost remote factory
void FLEffect::reset(){
    
    delete fSynchroTimer;
    delete fWatcher;
    
    if (fIsLocal)
        deleteDSPFactory(fFactory);
#ifdef REMOTE
    else
        deleteRemoteDSPFactory(fRemoteFactory);
#endif 
}

bool FLEffect::reinit(QString& error){
    return init(fCurrentSVGFolder, fCurrentIRFolder, fCurrentLibsFolder, fCompilationOptions, fOpt_level, error, fIpMachineRemote, fPortMachineRemote);
}

//Initialisation of the effect. From a source file, it builds the factory
//@param : currentSVGFolder = where to create the SVG-Folder tied to the factory 
//@param : currentIRFolder = where to save the bitcode tied to the factory
//@param : compilation Options = options to create the factory in faust compiler
//@param : optvalue = optimization value for LLVM compiler
//@param : error = if the initialisation fails, error is filled
//@param : ip/port remote = IP/Port of processing machine (Remote Case)
bool FLEffect::init(const QString& currentSVGFolder, const QString& currentIRFolder, const QString& currentLibsFolder, QString compilationMode, int optValue, QString& error, const QString& IPremote, int portremote){
    
    fCompilationOptions = compilationMode;
    fOpt_level = optValue;
    fIpMachineRemote = IPremote;
    fPortMachineRemote = portremote;

    fCurrentSVGFolder = currentSVGFolder;
    fCurrentIRFolder = currentIRFolder;
    fCurrentLibsFolder = currentLibsFolder;
    
    if(buildFactory(kCurrentFactory, error)){
    
        //Initializing watcher looking for changes on faust source through text editor 
        fWatcher = new QFileSystemWatcher(this);
        fSynchroTimer = new QTimer(fWatcher);
        connect(fSynchroTimer, SIGNAL(timeout()), this, SLOT(effectModified()));
        
        connect(fWatcher, SIGNAL(fileChanged(const QString)), this, SLOT(reset_Timer(const QString)));
        
        return true;
    }
    
    else
        return false;
}

//---------------FACTORY ACTIONS

//Force Recompilation ensures 
void FLEffect::forceRecompilation(bool val){
    fRecompilation = val;
}

bool FLEffect::hasToBeRecompiled(){
    
    if(fRecompilation){
        fRecompilation = false;
        return true;
    }
    else
        return false;
}   

//Creating the factory with the specific compilation options, in case of an error the buffer is filled
//@param : factoryToBuild = creating the current or charged factory
//@param : error = buffer to build in case build fails
//@param : currentSVGFolder = where to create the SVG-Folder tied to the factory 
//@param : currentIRFolder = where to save the bitcode tied to the factory
bool FLEffect::buildFactory(int factoryToBuild, QString& error){
    
    llvm_dsp_factory* buildingFactory = NULL;
    remote_dsp_factory* buildingRemoteFactory = NULL;
    
//To speed up the process of compilation, when a session is recalled, the DSP are re-compiled from Bitcode 
    QString IRpath = fCurrentIRFolder + "/" + fName;
    
    if(fRecalled && QFileInfo(IRpath).exists() && fIsLocal){        
//        buildingFactory = readDSPFactoryFromBitcodeFile(IRpath.toStdString(), "");
        
        fRecalled = false;
        
        if(buildingFactory != NULL){
            if(factoryToBuild == kCurrentFactory)
                fFactory = buildingFactory;
            else
                fOldFactory = buildingFactory;
            
            return true;
        }    
    }
    
    if(buildingFactory == NULL && buildingRemoteFactory == NULL){
        
        int numberFixedParams = 7;
		int iteratorParams = 0;
        
#ifdef _WIN32
		numberFixedParams = numberFixedParams+2;
#endif

        //+7 = -I libraryPath -I currentFolder -O drawPath -svg
        int argc = numberFixedParams;
        argc += get_numberParameters(fCompilationOptions);
        
        const char** argv = new const char*[argc];
        
        argv[iteratorParams] = "-I";
		iteratorParams++;
        
//The library path is where libraries like the scheduler architecture file are = currentSession
        string libPath = fCurrentLibsFolder.toStdString();
        argv[iteratorParams] = libPath.c_str();
		iteratorParams++;

        argv[iteratorParams] = "-I";   
		iteratorParams++;
        QString sourceChemin = QFileInfo(fSource).absolutePath();
		string sourcePath = sourceChemin.toStdString();

        argv[iteratorParams] = sourcePath.c_str();
		iteratorParams++;

        argv[iteratorParams] = "-O";
		iteratorParams++;
        
        QString svgPath = fCurrentSVGFolder + "/" + fName;
        
        QDir direct(svgPath);
        direct.mkdir(svgPath);
        
        string pathSVG = svgPath.toStdString();
        
        argv[iteratorParams] = pathSVG.c_str();
		iteratorParams++;
        argv[iteratorParams] = "-svg";
		iteratorParams++;
		
#ifdef _WIN32
//LLVM_MATH is added to resolve mathematical float functions, like powf
		argv[iteratorParams] = "-l";
		iteratorParams++;
		argv[iteratorParams] = "llvm_math.ll";
		iteratorParams++;
#endif
        
        //Parsing the compilationOptions from a string to a char**
        QString copy = fCompilationOptions;
        
        for(int i=numberFixedParams; i<argc; i++){
            
            string parseResult(parse_compilationParams(copy));
            
            char* intermediate = new char[parseResult.size()+1];
            
            strcpy(intermediate,parseResult.c_str());
            
//        OPTION DOUBLE HAS TO BE SKIPED, it causes segmentation fault
            if(strcmp(intermediate, "-double") != 0)
                argv[i] = (const char*)intermediate;
            else{
                argc--;
                i--;
            }
        }
        
        std::string getError("");
        
//        BUILDING THE FACTORY (Local or Remote)
        
        if(fIsLocal){      
            
			string mySourceToCompile = fSource.toStdString();

            buildingFactory = createDSPFactoryFromFile(mySourceToCompile, argc, argv, "", getError, fOpt_level);
        }
#ifdef REMOTE
        else             
            buildingRemoteFactory = createRemoteDSPFactoryFromFile(fSource.toStdString(), argc, argv, fIpMachineRemote.toStdString(), fPortMachineRemote, getError, fOpt_level);
#endif
        error = getError.c_str();
        
        delete[] argv;
        
        //The creation date is nedded in case the text editor sends a message of modification when actually the file has only been opened. It prevents recompilations for bad reasons
        fCreationDate = fCreationDate.currentDateTime();
        
        if(buildingFactory != NULL || buildingRemoteFactory != NULL){
            
//          KEEPING BUILT FACTORY IN RIGHT CLASS MEMBER
            if(fIsLocal){
                //The Bitcode files are written at each compilation 
//                writeDSPFactoryToBitcodeFile(buildingFactory, IRpath.toStdString());
                
                if(factoryToBuild == kCurrentFactory)
                    fFactory = buildingFactory;
                else
                    fOldFactory = buildingFactory;
            }
            else{
                if(factoryToBuild == kCurrentFactory)
                    fRemoteFactory = buildingRemoteFactory;
                else
                    fOldRemoteFactory = buildingRemoteFactory;
            }
            
            return true;
        }
        else
            return false;
    }
    else{
        fCreationDate = fCreationDate.currentDateTime();
        return false;
    }
} 

//Re-Build of the factory from the source file
//@param : error = buffer to build in case build fails
//@param : currentSVGFolder = where to create the SVG-Folder tied to the factory 
//@param : currentIRFolder = where to save the bitcode tied to the factory
bool FLEffect::update_Factory(QString& error){
    
    llvm_dsp_factory* local_factory_update = NULL;
    remote_dsp_factory* remote_factory_update = NULL;
    
	if(buildFactory(kChargingFactory, error)){
        
        if(fIsLocal){
			local_factory_update = fFactory;
			fFactory = fOldFactory;
			fOldFactory = local_factory_update;
		}
		else{
            remote_factory_update = fRemoteFactory;
			fRemoteFactory = fOldRemoteFactory;
			fOldRemoteFactory = remote_factory_update;
        }
        return true;
	}
	else
        return false;
}

//Once the rebuild is complete, the former factory has to be deleted
void FLEffect::erase_OldFactory(){
    
	if(fIsLocal) {
	    deleteDSPFactory(fOldFactory);
    }
#ifdef REMOTE
	else
        deleteRemoteDSPFactory(fOldRemoteFactory);
#endif
}

string FLEffect::get_expandedVersion(){
    
    string sha_key;
    string error_msg;
    
    string name_app = fName.toStdString();
    string dsp_content = pathToContent(fSource).toStdString();
    
    int numberFixedParams = 7;
    int iteratorParams = 0;
    
#ifdef _WIN32
    numberFixedParams = numberFixedParams+2;
#endif
    
    //+7 = -I libraryPath -I currentFolder -O drawPath -svg
    int argc = numberFixedParams;
    argc += get_numberParameters(fCompilationOptions);
    
    const char** argv = new const char*[argc];
    
    argv[iteratorParams] = "-I";
    iteratorParams++;
    
    //The library path is where libraries like the scheduler architecture file are = currentSession
    string libPath = fCurrentLibsFolder.toStdString();
    argv[iteratorParams] = libPath.c_str();
    iteratorParams++;
    
    argv[iteratorParams] = "-I";   
    iteratorParams++;
    string sourcePath = QFileInfo(fSource).absolutePath().toStdString();
    argv[iteratorParams] = sourcePath.c_str();
    iteratorParams++;
    
    argv[iteratorParams] = "-O";
    iteratorParams++;
    
    QString svgPath = fCurrentSVGFolder + "/" + fName;
    
    QDir direct(svgPath);
    direct.mkdir(svgPath);
    
    string pathSVG = svgPath.toStdString();
    
    argv[iteratorParams] = pathSVG.c_str();
    iteratorParams++;
    argv[iteratorParams] = "-svg";
    iteratorParams++;
    
#ifdef _WIN32
    //LLVM_MATH is added to resolve mathematical float functions, like powf
    argv[iteratorParams] = "-l";
    iteratorParams++;
    argv[iteratorParams] = "llvm_math.ll";
    iteratorParams++;
#endif
    
    //Parsing the compilationOptions from a string to a char**
    QString copy = fCompilationOptions;
    
    for(int i=numberFixedParams; i<argc; i++){
        
        string parseResult(parse_compilationParams(copy));
        
        char* intermediate = new char[parseResult.size()+1];
        
        strcpy(intermediate,parseResult.c_str());
        
        //        OPTION DOUBLE HAS TO BE SKIPED, it causes segmentation fault
        if(strcmp(intermediate, "-double") != 0)
            argv[i] = (const char*)intermediate;
        else{
            argc--;
            i--;
        }
    }

        
   return expandDSPFromString(name_app, dsp_content, argc, argv, sha_key, error_msg);
}

//---------------COMPILATION OPTIONS

//Get number of compilation options
int FLEffect::get_numberParameters(const QString& compilOptions){
    
    int argc = 0;
    int pos = compilOptions.indexOf("-");
    
    while(pos != -1 && pos < compilOptions.size()){
        
        argc++;
        
        pos = compilOptions.indexOf(" ", pos);
        
        while(pos != -1 && pos < compilOptions.size()){
            
            if(compilOptions[pos] == ' ')
                pos++;
            else if(compilOptions[pos] == '-')
                break;
            else{
                argc++;
                pos = compilOptions.indexOf(" ", pos);
            }
        }
    }

    return argc;
    
}

//Hand Made Parser
//Returns : the first option found, skipping the ' '
//CompilOptions : the rest of the options are kept in
string FLEffect::parse_compilationParams(QString& compilOptions){
    
    QString returning = "";
    
    int pos = 0;
    
    while(pos != -1 && pos < compilOptions.size()){
        if(compilOptions[pos] == ' ')
            pos++;
        else{
            int pos2 = compilOptions.indexOf(" ", pos);
            
            returning = compilOptions.mid(pos, pos2-pos);
            
            if(pos2 == -1)
                pos2 = compilOptions.size();
            compilOptions.remove(0, pos2);
            
            break;
        }
    }
    
    string returnin(returning.toStdString());
    
    return returnin;
}

//---------------WATCHER & FILE MODIFICATIONS ACTIONS

//When any action on the effect is performed, the watcher has to be stopped (and then re-launched) otherwise the synchronisation is called without good reason

void FLEffect::reset_Timer(const QString /*toto*/){
    
    //If the signal is triggered multiple times in 2 second, only 1 is taken into account
    if(fSynchroTimer->isActive()){
        fSynchroTimer->stop();
        fSynchroTimer->start(2000);
    }
    else
        fSynchroTimer->start(2000);
}

void FLEffect::effectModified(){
    fSynchroTimer->stop();
    emit effectChanged();
}

void FLEffect::stop_Watcher(){
    fWatcher->removePath(fSource);
}

void FLEffect::launch_Watcher(){
    
    fWatcher->addPath(fSource);
}

//--------------ACCESSORS

QString FLEffect::getSource(){
    return fSource;
}

void FLEffect::setSource(QString file){

//File:// is not liked by QFileInfo soooooo it has to be stripped off
	if(file.indexOf("file://") == 0){
		fSource = QString(file).right(file.size()-8);
	}
	else
		fSource = file;
}

QDateTime FLEffect::get_creationDate(){
    return fCreationDate;
}

QString FLEffect::getName(){
    
    return fName; 
}

void FLEffect::setName(QString name){
    fName = name;
}

llvm_dsp_factory* FLEffect::getFactory(){
    return fFactory;
}

remote_dsp_factory* FLEffect::getRemoteFactory(){
    return fRemoteFactory;
}

QString FLEffect::getCompilationOptions(){
    return fCompilationOptions;
}

void FLEffect::update_compilationOptions(QString& compilOptions, int newOptValue){
    if(fCompilationOptions.compare(compilOptions) !=0 || fOpt_level != newOptValue){
        
        fCompilationOptions = compilOptions;
        fOpt_level = newOptValue;
        fForceSynchro = true;
        emit effectChanged();
    }
}

void FLEffect::update_remoteMachine(const QString& ip, int port){
    
    fIpMachineRemote = ip;
    fPortMachineRemote = port;
    fForceSynchro = true;
    emit effectChanged();
}

int FLEffect::getOptValue(){
    return fOpt_level;
}

bool FLEffect::isSynchroForced(){
    return fForceSynchro;
}

void FLEffect::setForceSynchro(bool val){
    fForceSynchro = val;
}

bool FLEffect::isLocal(){
    return fIsLocal;
}

QString FLEffect::getRemoteIP(){
    return fIpMachineRemote;
}

int FLEffect::getRemotePort(){
    return fPortMachineRemote;
}