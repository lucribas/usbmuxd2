//
//  Manager.cpp
//  usbmuxd2
//
//  Created by tihmstar on 04.07.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "Manager.hpp"
#include <libgeneral/macros.h>
#include <libgeneral/exception.hpp>
#include "log.h"
#include <unistd.h>
#include <system_error>


Manager::Manager()
: _loopThread(nullptr),_loopState(LOOP_UNINITIALISED)
{
    //empty
}


Manager::~Manager(){
    debug("[Manager] destroying Manager(%p)",this);

    stopLoop();
    assert(_loopState == LOOP_UNINITIALISED || _loopState == LOOP_STOPPED);
}

void Manager::loopEvent(){
    reterror("[Manager] LoopEvent wasn't overwritten. Probably subclass construction failed!");
}


void Manager::startLoop(){
    retassure(_loopState == LOOP_UNINITIALISED, "[Manager] loop already initialized");
    cleanup([&]{
        _sleepy.unlock();
    });
    _sleepy.lock();

    loop_state expected = LOOP_UNINITIALISED;
    loop_state tobeplaced = LOOP_CONSTRUCTING;
    //if this fails, another thread is already creating a loop
    assure(_loopState.compare_exchange_strong(expected, tobeplaced));    
    assure(!_loopThread);

    
    {
    thread_retry:
        try {
            _loopThread = new std::thread([&]{
                _loopState = LOOP_RUNNING;
                _sleepy.unlock();
                while (_loopState == LOOP_RUNNING) {
                    try {
                        loopEvent();
                    } catch (tihmstar::exception &e) {
                        debug("[Manager] breaking Manager-Loop because of exception error=%s code=%d",e.what(),e.code());
                        break;
                    }
                }
                afterLoop();
                _loopState = LOOP_STOPPED;
            });
        } catch (std::system_error &e) {
            if (e.code() == std::errc::resource_unavailable_try_again) {
                error("[THREAD] creating thread threw EAGAIN! retrying in 5 seconds...");
                sleep(5);
                goto thread_retry;
            }
            error("[THREAD] got unhandled std::system_error %d (%s)",e.code().value(),e.exception::what());
            throw;
        }
    }

    //hangs here iff _loopThread didn't spawn yet
    _sleepy.lock();
    assure(_loopState == LOOP_RUNNING); //sanity check
}

void Manager::stopLoop() noexcept{
    if (_loopState < LOOP_STOPPED) {
        if (_loopState == LOOP_UNINITIALISED) {
            try{
                startLoop();                
            }catch (...){
                //we can't start the loop twice, but now we are sure it's been started at least once
            }
        }
        
        while (_loopState < LOOP_RUNNING) {
            //if we are constructing a thread, wait for it being constructed
            sched_yield();
        }
        
        while (!_loopThread) {
            //wait for _loopThread variable to be set
            sched_yield();
        }
        
        loop_state expected = LOOP_RUNNING;
        loop_state tobeplaced = LOOP_STOPPING;
        _loopState.compare_exchange_strong(expected, tobeplaced);
        
        stopAction();
        try {
            _loopThread->join();
        } catch (...) {
            //pass
        }
        delete _loopThread; _loopThread = nullptr;
    }
}

void Manager::afterLoop() noexcept{
    //do nothing by default
}

void Manager::stopAction() noexcept{
    //do nothing by default
}
