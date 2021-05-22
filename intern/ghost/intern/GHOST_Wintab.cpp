/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup GHOST
 */

#define _USE_MATH_DEFINES

#include "GHOST_Wintab.h"

#include <functional>

GHOST_WintabWin32 *GHOST_WintabWin32::loadWintab(HWND hwnd)
{
  /* Load Wintab library if available. */

  auto handle = unique_hmodule(::LoadLibrary("Wintab32.dll"), &::FreeLibrary);
  if (!handle) {
    return nullptr;
  }

  /* Get Wintab functions. */

  auto info = (GHOST_WIN32_WTInfo)::GetProcAddress(handle.get(), "WTInfoA");
  if (!info) {
    return nullptr;
  }

  auto open = (GHOST_WIN32_WTOpen)::GetProcAddress(handle.get(), "WTOpenA");
  if (!open) {
    return nullptr;
  }

  auto get = (GHOST_WIN32_WTGet)::GetProcAddress(handle.get(), "WTGetA");
  if (!get) {
    return nullptr;
  }

  auto set = (GHOST_WIN32_WTSet)::GetProcAddress(handle.get(), "WTSetA");
  if (!set) {
    return nullptr;
  }

  auto close = (GHOST_WIN32_WTClose)::GetProcAddress(handle.get(), "WTClose");
  if (!close) {
    return nullptr;
  }

  auto packetsGet = (GHOST_WIN32_WTPacketsGet)::GetProcAddress(handle.get(), "WTPacketsGet");
  if (!packetsGet) {
    return nullptr;
  }

  auto queueSizeGet = (GHOST_WIN32_WTQueueSizeGet)::GetProcAddress(handle.get(), "WTQueueSizeGet");
  if (!queueSizeGet) {
    return nullptr;
  }

  auto queueSizeSet = (GHOST_WIN32_WTQueueSizeSet)::GetProcAddress(handle.get(), "WTQueueSizeSet");
  if (!queueSizeSet) {
    return nullptr;
  }

  auto enable = (GHOST_WIN32_WTEnable)::GetProcAddress(handle.get(), "WTEnable");
  if (!enable) {
    return nullptr;
  }

  auto overlap = (GHOST_WIN32_WTOverlap)::GetProcAddress(handle.get(), "WTOverlap");
  if (!overlap) {
    return nullptr;
  }

  /* Build Wintab context. */

  LOGCONTEXT lc = {0};
  if (!info(WTI_DEFSYSCTX, 0, &lc)) {
    return nullptr;
  }

  modifyContext(lc);

  /* The Wintab spec says we must open the context disabled if we are using cursor masks. */
  auto hctx = unique_hctx(open(hwnd, &lc, FALSE), close);
  if (!hctx) {
    return nullptr;
  }

  /* Wintab provides no way to determine the maximum queue size aside from checking if attempts
   * to change the queue size are successful. */
  const int maxQueue = 500;
  int queueSize = queueSizeGet(hctx.get());

  while (queueSize < maxQueue) {
    int testSize = min(queueSize + 16, maxQueue);
    if (queueSizeSet(hctx.get(), testSize)) {
      queueSize = testSize;
    }
    else {
      /* From Windows Wintab Documentation for WTQueueSizeSet:
       * "If the return value is zero, the context has no queue because the function deletes the
       * original queue before attempting to create a new one. The application must continue
       * calling the function with a smaller queue size until the function returns a non - zero
       * value."
       *
       * In our case we start with a known valid queue size and in the event of failure roll
       * back to the last valid queue size. The Wintab spec dates back to 16 bit Windows, thus
       * assumes memory recently deallocated may not be available, which is no longer a practical
       * concern. */
      if (!queueSizeSet(hctx.get(), queueSize)) {
        /* If a previously valid queue size is no longer valid, there is likely something wrong in
         * the Wintab implementation and we should not use it. */
        return nullptr;
      }
      break;
    }
  }

  return new GHOST_WintabWin32(hwnd,
                               std::move(handle),
                               info,
                               get,
                               set,
                               packetsGet,
                               enable,
                               overlap,
                               std::move(hctx),
                               lc,
                               queueSize);
}

void GHOST_WintabWin32::modifyContext(LOGCONTEXT &lc)
{
  lc.lcPktData = PACKETDATA;
  lc.lcPktMode = PACKETMODE;
  lc.lcMoveMask = PACKETDATA;
  lc.lcOptions |= CXO_CSRMESSAGES | CXO_MESSAGES;

  /* Tablet scaling is handled manually because some drivers don't handle HIDPI or multi-display
   * correctly; reset tablet scale factors to unscaled tablet coodinates.
   *
   * Wintab maps y origin to the tablet's bottom; invert y to match Windows y origin mapping to the
   * screen top. */
  lc.lcOutOrgX = lc.lcInOrgX;
  lc.lcOutOrgY = lc.lcInOrgY;
  lc.lcOutExtX = lc.lcInExtX;
  lc.lcOutExtY = -lc.lcInExtY;
}

GHOST_WintabWin32::GHOST_WintabWin32(HWND hwnd,
                                     unique_hmodule handle,
                                     GHOST_WIN32_WTInfo info,
                                     GHOST_WIN32_WTGet get,
                                     GHOST_WIN32_WTSet set,
                                     GHOST_WIN32_WTPacketsGet packetsGet,
                                     GHOST_WIN32_WTEnable enable,
                                     GHOST_WIN32_WTOverlap overlap,
                                     unique_hctx hctx,
                                     LOGCONTEXT lc,
                                     int queueSize)
    : m_handle(std::move(handle)),
      m_fpInfo(info),
      m_fpGet(get),
      m_fpSet(set),
      m_fpPacketsGet(packetsGet),
      m_fpEnable(enable),
      m_fpOverlap(overlap),
      m_context(std::move(hctx)),
      m_logcontext(lc),
      m_pkts(queueSize)
{
  m_fpInfo(WTI_INTERFACE, IFC_NDEVICES, &numDevices);
  updateCursorInfo();
}

void GHOST_WintabWin32::enable()
{
  m_fpEnable(m_context.get(), true);
  m_fpOverlap(m_context.get(), true);
}

void GHOST_WintabWin32::disable()
{
  updateInRange(false);
  m_fpOverlap(m_context.get(), false);
  m_fpEnable(m_context.get(), false);
}

void GHOST_WintabWin32::updateInRange(bool inRange)
{
  m_inRange = inRange;
  if (!inRange) {
    m_buttons = 0;
    /* Clear the packet queue. */
    m_fpPacketsGet(m_context.get(), m_pkts.size(), m_pkts.data());
  }
}

void GHOST_WintabWin32::remapCoordinates()
{
  LOGCONTEXT lc = {0};

  if (m_fpInfo(WTI_DEFSYSCTX, 0, &lc)) {
    modifyContext(lc);
    m_logcontext = lc;

    m_fpSet(m_context.get(), &lc);
  }
}

void GHOST_WintabWin32::updateCursorInfo()
{
  AXIS Pressure, Orientation[3];

  BOOL pressureSupport = m_fpInfo(WTI_DEVICES, DVC_NPRESSURE, &Pressure);
  m_maxPressure = pressureSupport ? Pressure.axMax : 0;

  BOOL tiltSupport = m_fpInfo(WTI_DEVICES, DVC_ORIENTATION, &Orientation);
  /* Does the tablet support azimuth ([0]) and altitude ([1]). */
  if (tiltSupport && Orientation[0].axResolution && Orientation[1].axResolution) {
    m_maxAzimuth = Orientation[0].axMax;
    m_maxAltitude = Orientation[1].axMax;
  }
  else {
    m_maxAzimuth = m_maxAltitude = 0;
  }
}

void GHOST_WintabWin32::processInfoChange(LPARAM lParam)
{
  /* Update number of connected Wintab digitizers */
  if (LOWORD(lParam) == WTI_INTERFACE && HIWORD(lParam) == IFC_NDEVICES) {
    m_fpInfo(WTI_INTERFACE, IFC_NDEVICES, &numDevices);
  }
}

/**
 * TODO
 */
bool GHOST_WintabWin32::devicesPresent()
{
  return numDevices;
}

void GHOST_WintabWin32::getInput(std::vector<GHOST_WintabInfoWin32> &outWintabInfo)
{
  const int numPackets = m_fpPacketsGet(m_context.get(), m_pkts.size(), m_pkts.data());
  outWintabInfo.resize(numPackets);

  /*  */
  std::function<int(LONG)> scaleX;
  LOGCONTEXT &lc = m_logcontext;
  if ((lc.lcInExtX < 0) == (lc.lcSysExtX < 0)) {
    scaleX = [&](LONG inX) {
      return (inX - lc.lcInOrgX) * abs(lc.lcSysExtX) / abs(lc.lcInExtX) + lc.lcSysOrgX;
    };
  }
  else {
    scaleX = [&](LONG inX) {
      return (abs(lc.lcInExtX) - (inX - lc.lcInOrgX)) * abs(lc.lcSysExtX) / abs(lc.lcInExtX) +
             lc.lcSysOrgX;
    };
  }

  std::function<int(LONG)> scaleY;
  if ((lc.lcInExtY < 0) == (lc.lcSysExtY < 0)) {
    scaleY = [&](LONG inY) {
      return (inY - lc.lcInOrgY) * abs(lc.lcSysExtY) / abs(lc.lcInExtY) + lc.lcSysOrgY;
    };
  }
  else {
    scaleY = [&](LONG inY) {
      return (abs(lc.lcInExtY) - (inY - lc.lcInOrgY)) * abs(lc.lcSysExtY) / abs(lc.lcInExtY) +
             lc.lcSysOrgY;
    };
  }

  for (int i = 0; i < numPackets; i++) {
    PACKET pkt = m_pkts[i];
    GHOST_WintabInfoWin32 &out = outWintabInfo[i];

    out.tabletData = GHOST_TABLET_DATA_NONE;
    /* % 3 for multiple devices ("DualTrack"). */
    switch (pkt.pkCursor % 3) {
      case 0:
        /* Puck - processed as mouse. */
        out.tabletData.Active = GHOST_kTabletModeNone;
        break;
      case 1:
        out.tabletData.Active = GHOST_kTabletModeStylus;
        break;
      case 2:
        out.tabletData.Active = GHOST_kTabletModeEraser;
        break;
    }

    out.x = scaleX(pkt.pkX);
    out.y = scaleY(pkt.pkY);

    if (m_maxPressure > 0) {
      out.tabletData.Pressure = (float)pkt.pkNormalPressure / (float)m_maxPressure;
    }

    if ((m_maxAzimuth > 0) && (m_maxAltitude > 0)) {
      ORIENTATION ort = pkt.pkOrientation;
      float vecLen;
      float altRad, azmRad; /* In radians. */

      /*
       * From the wintab spec:
       * orAzimuth: Specifies the clockwise rotation of the cursor about the z axis through a
       * full circular range.
       * orAltitude: Specifies the angle with the x-y plane through a signed, semicircular range.
       * Positive values specify an angle upward toward the positive z axis; negative values
       * specify an angle downward toward the negative z axis.
       *
       * wintab.h defines orAltitude as a UINT but documents orAltitude as positive for upward
       * angles and negative for downward angles. WACOM uses negative altitude values to show that
       * the pen is inverted; therefore we cast orAltitude as an (int) and then use the absolute
       * value.
       */

      /* Convert raw fixed point data to radians. */
      altRad = (float)((fabs((float)ort.orAltitude) / (float)m_maxAltitude) * M_PI / 2.0);
      azmRad = (float)(((float)ort.orAzimuth / (float)m_maxAzimuth) * M_PI * 2.0);

      /* Find length of the stylus' projected vector on the XY plane. */
      vecLen = cos(altRad);

      /* From there calculate X and Y components based on azimuth. */
      out.tabletData.Xtilt = sin(azmRad) * vecLen;
      out.tabletData.Ytilt = (float)(sin(M_PI / 2.0 - azmRad) * vecLen);
    }

    /* Some Wintab libraries don't handle relative button input, so we track button presses
     * manually. */
    out.button = GHOST_kButtonMaskNone;
    out.type = GHOST_kEventCursorMove;

    DWORD buttonsChanged = m_buttons ^ pkt.pkButtons;
    if (buttonsChanged) {
      /* Find the index for the changed button from the button map. */
      WORD physicalButton = 0;
      for (DWORD diff = (unsigned)buttonsChanged >> 1; diff > 0; diff = (unsigned)diff >> 1) {
        physicalButton++;
      }

      out.button = mapWintabToGhostButton(pkt.pkCursor, physicalButton);

      if (out.button != GHOST_kButtonMaskNone) {
        if (buttonsChanged & pkt.pkButtons) {
          out.type = GHOST_kEventButtonDown;
        }
        else {
          out.type = GHOST_kEventButtonUp;
        }
      }

      /* Only update handled button, in case multiple button events arrived simultaneously. */
      m_buttons ^= 1 << physicalButton;
    }

    out.time = pkt.pkTime;
  }

  if (!outWintabInfo.empty()) {
    // lastTabletData = outWintabInfo.back().tabletData;
  }
}

GHOST_TButtonMask GHOST_WintabWin32::mapWintabToGhostButton(UINT cursor, WORD physicalButton)
{
  const WORD numButtons = 32;
  BYTE logicalButtons[numButtons] = {0};
  BYTE systemButtons[numButtons] = {0};

  if (!m_fpInfo(WTI_CURSORS + cursor, CSR_BUTTONMAP, &logicalButtons) ||
      !m_fpInfo(WTI_CURSORS + cursor, CSR_SYSBTNMAP, &systemButtons)) {
    return GHOST_kButtonMaskNone;
  }

  if (physicalButton >= numButtons) {
    return GHOST_kButtonMaskNone;
  }

  BYTE lb = logicalButtons[physicalButton];

  if (lb >= numButtons) {
    return GHOST_kButtonMaskNone;
  }

  switch (systemButtons[lb]) {
    case SBN_LCLICK:
      return GHOST_kButtonMaskLeft;
    case SBN_RCLICK:
      return GHOST_kButtonMaskRight;
    case SBN_MCLICK:
      return GHOST_kButtonMaskMiddle;
    default:
      return GHOST_kButtonMaskNone;
  }
}
