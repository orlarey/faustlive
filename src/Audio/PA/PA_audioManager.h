//
//  PA_audioManager.h
//  
//
//  Created by Sarah Denoux on 15/07/13.
//  Copyright (c) 2013 __MyCompanyName__. All rights reserved.
//

// PA_audioManager controls 1 PA_audioFader. It can switch from one DSP to another with a crossfade or it can act like a simple jackaudio-dsp
// PA_audioManager also controls the jack connections of the audio. 

#ifndef _PA_audioManager_h
#define _PA_audioManager_h

#include "AudioManager.h"
#include "FJUI.h"

class PA_audioFader;

class PA_audioManager : public AudioManager {

    private:
    
        Q_OBJECT
        
        bool fInit;
        
        PA_audioFader* fCurrentAudio;
        PA_audioFader* fFadeInAudio;

    public:
    
        PA_audioManager(AudioShutdownCallback cb, void* arg);
        virtual ~PA_audioManager();
    
        virtual bool initAudio(QString&, const char*, bool midi);
        virtual bool initAudio(QString& error, const char* name, const char* port_name, int numInputs, int numOutputs, bool midi);
        virtual bool setDSP(QString& error, dsp* DSP, const char* port_name);
        
        virtual bool init(const char*, dsp* DSP);
        virtual bool start();
        virtual void stop();
        
        virtual bool init_FadeAudio(QString& error, const char* name, dsp* DSP);
        virtual void start_Fade();
        virtual void wait_EndFade();

        virtual int getBufferSize();
        virtual int getSampleRate();
};

#endif
