/*
 *      Copyright (C) 2015 Team Kodi
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"

#if defined(TARGET_ANDROID)
#include "utils/log.h"
#include "VideoSyncAndroid.h"
#include "video/VideoReferenceClock.h"
#include "utils/TimeUtils.h"
#include "android/activity/XBMCApp.h"
#include "windowing/WindowingFactory.h"
#include "guilib/GraphicContext.h"
#include "utils/MathUtils.h"


bool CVideoSyncAndroid::Setup(PUPDATECLOCK func)
{
  CLog::Log(LOGDEBUG, "CVideoSyncAndroid::%s setting up", __FUNCTION__);
  
  //init the vblank timestamp
  m_LastVBlankTime = CurrentHostCounter();
  UpdateClock = func;
  m_abort = false;
  m_emulatedVsync = false;
  m_emulatedVsyncDurationNs = g_VideoReferenceClock.GetFrequency() / GetFPS();
  m_vecVsync.clear();
  
  CXBMCApp::InitFrameCallback(this);
  g_Windowing.Register(this);

  return true;
}

void CVideoSyncAndroid::Run(volatile bool& stop)
{
  // vblank callback, we just keep sleeping until we're asked to stop the thread
  while(!stop && !m_abort)
  {
    if (m_emulatedVsync)
    {
      usleep(m_emulatedVsyncDurationNs);
      uint64_t now = CurrentHostCounter();
      UpdateClock(1, now);
    }
    else
    {
      // Check if the Choregrapher clock is inline with the refresh rate
      int64_t averageVsyncNs = 0;
      for (auto n : m_vecVsync)
      {
        averageVsyncNs += n;
      }
      averageVsyncNs /= m_vecVsync.size();

      if (m_vecVsync >= AVERAGE_VSYNC_NUM )
      {
        double r =  (double)averageVsyncNs / m_emulatedVsyncDurationNs;
        if (r < 0.8 || r > 1.2)
        {
          // Nope. Let's shift to emulated vsync
          CLog::Log(LOGWARNING, "CVideoSyncAndroid::%s too big error (%f): going to emulated", __FUNCTION__, r);

          m_emulatedVsync = true;
          CXBMCApp::DeinitFrameCallback();
          continue;
        }
      }

      Sleep(100);
    }
  }
}

void CVideoSyncAndroid::Cleanup()
{
  CLog::Log(LOGDEBUG, "CVideoSyncAndroid::%s cleaning up", __FUNCTION__);
  CXBMCApp::DeinitFrameCallback();
  g_Windowing.Unregister(this);
}

float CVideoSyncAndroid::GetFps()
{
  m_fps = g_graphicsContext.GetFPS();
  CLog::Log(LOGDEBUG, "CVideoSyncAndroid::%s Detected refreshrate: %f hertz", __FUNCTION__, m_fps);
  return m_fps;
}

void CVideoSyncAndroid::OnResetDevice()
{
  m_abort = true;
}

void CVideoSyncAndroid::FrameCallback(int64_t frameTimeNanos)
{
  int           NrVBlanks;
  double        VBlankTime;
  int64_t       nowtime = CurrentHostCounter();
  
  //calculate how many vblanks happened
  int64_t FT = (frameTimeNanos - m_LastVBlankTime);
  m_vecVsync.push_back(FT);
  if (m_vecVsync.size() > AVERAGE_VSYNC_NUM)
    m_vecVsync.erase(m_vecVsync.begin());
  VBlankTime = FT / (double)g_VideoReferenceClock.GetFrequency();
  NrVBlanks = MathUtils::round_int(VBlankTime * m_fps);
  
  // CLog::Log(LOGDEBUG, "CVideoSyncAndroid::FrameCallback %lld(%f fps), %d", FT, 1.0/((double)FT/1000000000), NrVBlanks);

  //save the timestamp of this vblank so we can calculate how many happened next time
  m_LastVBlankTime = frameTimeNanos;
  
  //update the vblank timestamp, update the clock and send a signal that we got a vblank
  UpdateClock(NrVBlanks, nowtime);
}

#endif //TARGET_ANDROID
