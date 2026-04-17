/*
 * Copyright 2001-2011 Vedder Bruno.
 *
 * This file is part of Osmose, a Sega Master System/Game Gear software
 * emulator.
 *
 * Osmose is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * Osmose is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Osmose.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * File: SoundThread.cpp
 *
 * Description:
 *
 * Author: Bruno Vedder
 * Date: Wed Dec 15 07:43:05 2010
 *
 * URL: http://bcz.asterope.fr
 */

#include "SoundThread.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

/**
 * Constructor.
 */
SoundThread::SoundThread(const char *devName, FIFOSoundBuffer *sb)
{
    playback_handle = NULL;
    hw_params = NULL;
    sw_params = NULL;
    frames_to_deliver = 0;
    interleavedAccess = false;

    // Make a deep copy of the device name
    strncpy(deviceName, devName, DEVICE_NAME_LENGTH);
    deviceName[DEVICE_NAME_LENGTH - 1] = '\0';
    initAlsa();
    state = Paused;
    mutex = PTHREAD_MUTEX_INITIALIZER;
    sndFIFO = sb;
}

/**
 * Destructor.
 */
SoundThread::~SoundThread()
{

    // Set state to stopped and join ourself.
    state = Stopped;
    this->join(NULL);

    // THEN, close the audio device.
    if (playback_handle != NULL)
    {
        snd_pcm_close(playback_handle);
    }
}

/**
 * This is the main Sound thread loop.
 */
void* SoundThread::run(void *p)
{
    (void)p;

    SoundThreadState local_state_copy;

    {
        MutexLocker lock(&mutex);
        local_state_copy = state;
    }


    while(local_state_copy != Stopped)
    {
        switch(local_state_copy)
        {

            case Playing:
                play();
            break;

            case Paused:
                struct timespec rqtp;
                rqtp.tv_sec = 0;
                rqtp.tv_nsec = 1000000; // 1 millisecond.
                nanosleep(&rqtp, NULL); // NULL = don't care about remaining time if interrupted.
            break;

            default:
                // Stopped means that thread is terminating.
            break;
        }

        {   // Locked section.
            MutexLocker lock(&mutex);
            local_state_copy = state;
        }
    }

    // We are Leaving the thread.
    return (void *)0xDEADBEEF;
}

void SoundThread::play()
{
    int err;
    // Wait till the interface is ready for data, or 16 milli second
    // has elapsed.

    if ((err = snd_pcm_wait(playback_handle, 16)) < 0)
    {
        err = snd_pcm_recover(playback_handle, err, 1);
        if (err < 0)
        {
            fprintf(stderr, "poll failed (%s)\n", strerror(errno));
            return;
        }
    }

    // find out how much space is available for playback data

    if ((frames_to_deliver = snd_pcm_avail_update (playback_handle)) < 0)
    {
        err = snd_pcm_recover(playback_handle, (int)frames_to_deliver, 1);
        if (err < 0)
        {
            fprintf (stderr, "unknown ALSA avail update return value (%d)\n",
                     (int)frames_to_deliver);
        }
        return;
    }

    frames_to_deliver = frames_to_deliver > 4096 ? 4096 : frames_to_deliver;
    if (frames_to_deliver <= 0)
    {
        return;
    }

    // deliver the data

    if (playback_callback (frames_to_deliver) != frames_to_deliver)
    {
        fprintf (stderr, "playback callback failed\n");
    }
}


int SoundThread::playback_callback (snd_pcm_sframes_t nframes)
{
    //printf ("playback callback called with %d frames\n", (int)nframes);
    sndFIFO->read(samplebuffer, nframes);

    return writeFrames(samplebuffer, nframes);
}

int SoundThread::writeFrames(const short *buffer, snd_pcm_sframes_t nframes)
{
    snd_pcm_sframes_t written = 0;

    while (written < nframes)
    {
        snd_pcm_sframes_t err;
        snd_pcm_sframes_t remaining = nframes - written;

        if (interleavedAccess)
        {
            err = snd_pcm_writei(playback_handle, buffer + written, remaining);
        }
        else
        {
            void *channelsbuffer[1];
            channelsbuffer[0] = (void *)(buffer + written);
            err = snd_pcm_writen(playback_handle, (void **)channelsbuffer, remaining);
        }

        if (err == -EAGAIN)
        {
            continue;
        }

        if (err < 0)
        {
            err = snd_pcm_recover(playback_handle, (int)err, 1);
            if (err < 0)
            {
                fprintf(stderr, "write failed (%s)\n", snd_strerror((int)err));
                return (int)err;
            }
            continue;
        }

        if (err == 0)
        {
            break;
        }

        written += err;
    }

    return (int)written;
}


/**
 *
 */
void SoundThread::stop()
{
    MutexLocker lock(&mutex);
    state = Stopped;

    // Perform ALSA shutdown!
}

/**
 *
 */
void SoundThread::pause()
{
    MutexLocker lock(&mutex);
    state = Paused;

    // Perform ALSA Pause
}

/**
 *
 */
void SoundThread::resume()
{
    MutexLocker lock(&mutex);
    state = Playing;
    // Perform ALSA start/continue!
}




/**
 * This method prepares ALSA system for 22050hz signed 16bits Little Endian
 * playback.
 */
void SoundThread::initAlsa()
{
    int err;
    int openErr = 0;
    ostringstream oss;
    vector<string> candidates;
    const char *envDevice = getenv("OSMOSE_AUDIO_DEVICE");

    auto addCandidate = [&candidates](const char *name)
    {
        if ((name == NULL) || (name[0] == '\0'))
        {
            return;
        }

        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (candidates[i] == name)
            {
                return;
            }
        }
        candidates.push_back(name);
    };

    addCandidate(envDevice);
    if ((strcmp(deviceName, "auto") != 0) && (strcmp(deviceName, "AUTO") != 0))
    {
        addCandidate(deviceName);
    }
    addCandidate("default");
    addCandidate("pipewire");
    addCandidate("pulse");
    addCandidate("plughw:0,0");

    // Get a handle on the PCM device.
    for (size_t i = 0; i < candidates.size(); i++)
    {
        err = snd_pcm_open(&playback_handle, candidates[i].c_str(), SND_PCM_STREAM_PLAYBACK, 0);
        if (err >= 0)
        {
            openedDeviceName = candidates[i];
            break;
        }
        openErr = err;
    }

    if (playback_handle == NULL)
    {
        oss << "cannot open audio device";
        if (envDevice != NULL && envDevice[0] != '\0')
        {
            oss << " (OSMOSE_AUDIO_DEVICE=" << envDevice << ")";
        }
        oss << ": " << snd_strerror(openErr) << endl;
        throw oss.str();
    }

    // Allocate snd_pcm_hw_params_t structure.
    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0)
    {
        oss << "cannot allocate hardware parameter structure: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    // Retrieve current parameters.
    if ((err = snd_pcm_hw_params_any (playback_handle, hw_params)) < 0)
    {
        oss << "cannot initialize hardware parameter structure: " << snd_strerror (err) << endl;
        throw oss.str();
    }


    // Prefer non interleaved mode, but allow interleaved for modern ALSA backends.
    err = snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_NONINTERLEAVED);
    if (err < 0)
    {
        err = snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0)
        {
            oss << "cannot set access type: " << snd_strerror(err) << endl;
            throw oss.str();
        }
        interleavedAccess = true;
    }
    else
    {
        interleavedAccess = false;
    }

    // Set Sample format: Signed 16bit little endian.
    if ((err = snd_pcm_hw_params_set_format (playback_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0)
    {
        oss << "cannot set sample format: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    // Set the Sample rate.
    if ((err = snd_pcm_hw_params_set_rate (playback_handle, hw_params, 22050, 0)) < 0)
    {
        oss << "cannot set sample rate: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    // Set Channel number (MONO).
    if ((err = snd_pcm_hw_params_set_channels (playback_handle, hw_params, 1)) < 0)
    {
        oss << "cannot set channel count: " << snd_strerror (err) << endl;
        throw oss.str();
    }


    if ((err = snd_pcm_hw_params_set_buffer_size(playback_handle, hw_params, 2048)) < 0)
    {
        oss << "cannot set channel buffer size: " << snd_strerror (err) << endl;
        throw oss.str();
    }



    // Apply these parameters.
    if ((err = snd_pcm_hw_params (playback_handle, hw_params)) < 0)
    {
        oss << "cannot apply parameters: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    snd_pcm_uframes_t bufferSize;
    snd_pcm_hw_params_get_buffer_size( hw_params, &bufferSize );
    //cout << "initAlsa: Buffer size = " << bufferSize << " frames." << endl;

    // Free memory allocated for snd_pcm_hw_params_t
    snd_pcm_hw_params_free (hw_params);

    /* tell ALSA to wake us up whenever 4096 or more frames
       of playback data can be delivered. Also, tell
       ALSA that we'll start the device ourselves.
    */

    // Allocate snd_pcm_sw_params_t structure.
    if ((err = snd_pcm_sw_params_malloc (&sw_params)) < 0)
    {
        oss << "cannot allocate software parameters structure: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    // Get the current software configuration
    if ((err = snd_pcm_sw_params_current (playback_handle, sw_params)) < 0)
    {
        oss << "cannot initialize software parameters structure: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    // Set the wake up point to 2048 (92.9 ms). The minimum data available before asking
    // for new ones.
    if ((err = snd_pcm_sw_params_set_avail_min (playback_handle, sw_params, 2048U)) < 0)
    {
        oss << "cannot set minimum available count: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    // Set when ALSA starts to play.
    if ((err = snd_pcm_sw_params_set_start_threshold (playback_handle, sw_params, 1024U)) < 0)
    {
        oss << "cannot set start mode: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    // Apply parameters.
    if ((err = snd_pcm_sw_params (playback_handle, sw_params)) < 0)
    {
        oss << "cannot apply software parameters: " << snd_strerror (err) << endl;
        throw oss.str();
    }

    /* the interface will interrupt the kernel every 4096 frames, and ALSA
       will wake up this program very soon after that.
    */

    if ((err = snd_pcm_prepare (playback_handle)) < 0)
    {
        oss << "cannot prepare audio interface for use: " << snd_strerror (err) << endl;
        throw oss.str();
    }
}

const char *SoundThread::getOpenedDeviceName() const
{
    return openedDeviceName.c_str();
}
