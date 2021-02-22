/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 *           (C) 2009, 2021 HIRAOKA HYPERS TOOLS, Inc.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#include <map>

#define XK_MISCELLANY
#define XK_LATIN1
#define XK_CURRENCY
#include <rfb/keysymdef.h>

#include <rfb_win32/CKeyboard.h>
#include <rfb/LogWriter.h>
#include <rfb_win32/OSVersion.h>
#include "keymap.h"

using namespace rfb;

static LogWriter vlog("CKeyboard");


// Client-side RFB keyboard event sythesis

class CKeymapper {

public:
  CKeymapper()
  {
    for (int i = 0; i < sizeof(keymap) / sizeof(keymap_t); i++) {
      int extendedVkey = keymap[i].vk + (keymap[i].extended ? 256 : 0);
      if (keysymMap.find(extendedVkey) == keysymMap.end()) {
        keysymMap[extendedVkey] = keymap[i].keysym;
      }
    }
  }

  // lookup() tries to find a match for vkey with the extended flag.  We check
  // first for an exact match including the extended flag, then try without the
  // extended flag.
  rdr::U32 lookup(int extendedVkey) {
    if (keysymMap.find(extendedVkey) != keysymMap.end())
      return keysymMap[extendedVkey];
    if (keysymMap.find(extendedVkey ^ 256) != keysymMap.end())
      return keysymMap[extendedVkey ^ 256];
    return 0;
  }

private:
  std::map<int,rdr::U32> keysymMap;
} ckeymapper;


class ModifierKeyReleaser {
public:
  ModifierKeyReleaser(InputHandler* writer_, int vkCode, bool extended)
    : writer(writer_), extendedVkey(vkCode + (extended ? 256 : 0)),
      keysym(0)
  {}
  void release(std::map<int,rdr::U32>* downKeysym) {
    if (downKeysym->find(extendedVkey) != downKeysym->end()) {
      keysym = (*downKeysym)[extendedVkey];
      vlog.debug("fake release extendedVkey 0x%x, keysym 0x%x",
                 extendedVkey, keysym);
      writer->keyEvent(keysym, false);
    }
  }
  ~ModifierKeyReleaser() {
    if (keysym) {
      vlog.debug("fake press extendedVkey 0x%x, keysym 0x%x",
                 extendedVkey, keysym);
      writer->keyEvent(keysym, true);
    }
  }
  InputHandler* writer;
  int extendedVkey;
  rdr::U32 keysym;
};

// IS_PRINTABLE_LATIN1 tests if a character is either a printable latin1
// character, or 128, which is the Euro symbol on Windows.
#define IS_PRINTABLE_LATIN1(c) (((c) >= 32 && (c) <= 126) || (c) == 128 || \
                                ((c) >= 160 && (c) <= 255))

void win32::CKeyboard::keyEvent(InputHandler* writer, rdr::U8 vkey,
                                rdr::U32 flags, bool down)
{
  bool extended = (flags & 0x1000000);
  int extendedVkey = vkey + (extended ? 256 : 0);

  // If it's a release, just release whichever keysym corresponded to the same
  // key being pressed, regardless of how it would be interpreted in the
  // current keyboard state.
  if (!down) {
    releaseKey(writer, extendedVkey);
    return;
  }

  // We should always pass every down event to ToAscii() otherwise it can get
  // out of sync.

  // XXX should we pass CapsLock, ScrollLock or NumLock to ToAscii - they
  // actually alter the lock state on the keyboard?

  BYTE keystate[256];
  GetKeyboardState(keystate);
  rdr::U8 chars[2];

  int nchars = ToAscii(vkey, 0, keystate, (WORD*)&chars, 0);

  // See if it's in the Windows VK code -> X keysym map.  We do this before
  // looking at the result of ToAscii so that e.g. we recognise that it's
  // XK_KP_Add rather than '+'.

  rdr::U32 keysym = ckeymapper.lookup(extendedVkey);
  if (keysym) {
    vlog.debug("mapped key: extendedVkey 0x%x", extendedVkey);
    pressKey(writer, extendedVkey, keysym);
    return;
  }

  if (nchars < 0) {
    // Dead key - the next call to ToAscii() will give us either the accented
    // character or two characters.
    vlog.debug("ToAscii dead key (1): extendedVkey 0x%x", extendedVkey);
    return;
  }

  if (nchars > 0 && IS_PRINTABLE_LATIN1(chars[0])) {
    // Got a printable latin1 character.  We must release Control and Alt
    // (AltGr) if they were both pressed, so that the latin1 character is seen
    // without them by the VNC server.
    ModifierKeyReleaser lctrl(writer, VK_CONTROL, 0);
    ModifierKeyReleaser rctrl(writer, VK_CONTROL, 1);
    ModifierKeyReleaser lalt(writer, VK_MENU, 0);
    ModifierKeyReleaser ralt(writer, VK_MENU, 1);

    if ((keystate[VK_CONTROL] & 0x80) && (keystate[VK_MENU] & 0x80)) {
      lctrl.release(&downKeysym);
      rctrl.release(&downKeysym);
      lalt.release(&downKeysym);
      ralt.release(&downKeysym);
    }

    for (int i = 0; i < nchars; i++) {
      vlog.debug("ToAscii key (1): extendedVkey 0x%x", extendedVkey);
      if (chars[i] == 128) { // special hack for euro!
        pressKey(writer, extendedVkey, XK_EuroSign);
      } else {
        switch (altKeyCode) {
          case 0:
			switch (chars[i]) {
			  case 33: // !
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 33);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 34: // "
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 34);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 35: // #
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 35);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 36: // $
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 36);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 37: // %
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 37);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 38: // &
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 38);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 40: // (
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 40);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 41: // )
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 41);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 42: // *
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 42);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 43: // +
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 43);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 58: // :
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 58);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 60: // <
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 60);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 62: // >
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 62);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 63: // ?
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 63);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 64: // @
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 64);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 65: // A
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 65);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 66: // B
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 66);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 67: // C
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 67);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 68: // D
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 68);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 69: // E
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 69);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 70: // F
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 70);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 71: // G
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 71);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 72: // H
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 72);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 73: // I
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 73);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 74: // J
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 74);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 75: // K
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 75);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 76: // L
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 76);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 77: // M
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 77);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 78: // N
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 78);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 79: // O
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 79);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 80: // P
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 80);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 81: // Q
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 81);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 82: // R
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 82);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 83: // S
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 83);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 84: // T
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 84);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 85: // U
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 85);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 86: // V
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 86);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 87: // W
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 87);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 88: // X
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 88);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 89: // Y
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 89);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 90: // Z
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 90);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 94: // ^
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 94);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 95: // _
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 95);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 123: // {
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 123);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 124: // |
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 124);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 125: // }
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 125);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  case 126: // ~
				pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
				pressKey(writer, extendedVkey, 126);
				releaseKey(writer, 0|VK_SHIFT);
				break;
			  default:
				pressKey(writer, extendedVkey, chars[i]);
				break;
		    }
			break;
		  case 1:
            switch (chars[i]) {
              case 33: // !
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 33);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 34: // "
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 50);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 35: // #
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 35);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 36: // $
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 36);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 37: // %
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 37);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 38: // &
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 54);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 39: // '
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 38);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 40: // (
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 42);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 41: // )
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 40);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 42: // *
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 34);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 43: // +
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 58);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 44: // ,
                pressKey(writer, extendedVkey, 44);
                break;
              case 45: // -
                pressKey(writer, extendedVkey, 45);
                break;
              case 46: // .
                pressKey(writer, extendedVkey, 46);
                break;
              case 47: // /
                pressKey(writer, extendedVkey, 47);
                break;
              case 48: // 0
                pressKey(writer, extendedVkey, 41);
                break;
              case 49: // 1
                pressKey(writer, extendedVkey, 33);
                break;
              case 50: // 2
                pressKey(writer, extendedVkey, 50);
                break;
              case 51: // 3
                pressKey(writer, extendedVkey, 35);
                break;
              case 52: // 4
                pressKey(writer, extendedVkey, 36);
                break;
              case 53: // 5
                pressKey(writer, extendedVkey, 37);
                break;
              case 54: // 6
                pressKey(writer, extendedVkey, 54);
                break;
              case 55: // 7
                pressKey(writer, extendedVkey, 38);
                break;
              case 56: // 8
                pressKey(writer, extendedVkey, 42);
                break;
              case 57: // 9
                pressKey(writer, extendedVkey, 40);
                break;
              case 58: // :
                pressKey(writer, extendedVkey, 34);
                break;
              case 59: // ;
                pressKey(writer, extendedVkey, 58);
                break;
              case 60: // <
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 44);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 61: // =
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 45);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 62: // >
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 46);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 63: // ?
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 47);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 64: // @
                pressKey(writer, extendedVkey, 91);
                break;
              case 65: // A
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 65);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 66: // B
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 66);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 67: // C
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 67);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 68: // D
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 68);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 69: // E
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 69);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 70: // F
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 70);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 71: // G
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 71);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 72: // H
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 72);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 73: // I
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 73);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 74: // J
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 74);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 75: // K
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 75);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 76: // L
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 76);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 77: // M
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 77);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 78: // N
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 78);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 79: // O
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 79);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 80: // P
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 80);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 81: // Q
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 81);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 82: // R
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 82);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 83: // S
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 83);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 84: // T
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 84);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 85: // U
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 85);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 86: // V
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 86);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 87: // W
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 87);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 88: // X
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 88);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 89: // Y
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 89);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 90: // Z
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 90);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 91: // [
                pressKey(writer, extendedVkey, 93);
                break;
              case 93: // ]
                pressKey(writer, extendedVkey, 92);
                break;
              case 94: // ^
                pressKey(writer, extendedVkey, 43);
                break;
              case 96: // `
                pressKey(writer, extendedVkey, 96);
                break;
              case 97: // a
                pressKey(writer, extendedVkey, 65);
                break;
              case 98: // b
                pressKey(writer, extendedVkey, 66);
                break;
              case 99: // c
                pressKey(writer, extendedVkey, 67);
                break;
              case 100: // d
                pressKey(writer, extendedVkey, 68);
                break;
              case 101: // e
                pressKey(writer, extendedVkey, 69);
                break;
              case 102: // f
                pressKey(writer, extendedVkey, 70);
                break;
              case 103: // g
                pressKey(writer, extendedVkey, 71);
                break;
              case 104: // h
                pressKey(writer, extendedVkey, 72);
                break;
              case 105: // i
                pressKey(writer, extendedVkey, 73);
                break;
              case 106: // j
                pressKey(writer, extendedVkey, 74);
                break;
              case 107: // k
                pressKey(writer, extendedVkey, 75);
                break;
              case 108: // l
                pressKey(writer, extendedVkey, 76);
                break;
              case 109: // m
                pressKey(writer, extendedVkey, 77);
                break;
              case 110: // n
                pressKey(writer, extendedVkey, 78);
                break;
              case 111: // o
                pressKey(writer, extendedVkey, 79);
                break;
              case 112: // p
                pressKey(writer, extendedVkey, 80);
                break;
              case 113: // q
                pressKey(writer, extendedVkey, 81);
                break;
              case 114: // r
                pressKey(writer, extendedVkey, 82);
                break;
              case 115: // s
                pressKey(writer, extendedVkey, 83);
                break;
              case 116: // t
                pressKey(writer, extendedVkey, 84);
                break;
              case 117: // u
                pressKey(writer, extendedVkey, 85);
                break;
              case 118: // v
                pressKey(writer, extendedVkey, 86);
                break;
              case 119: // w
                pressKey(writer, extendedVkey, 87);
                break;
              case 120: // x
                pressKey(writer, extendedVkey, 88);
                break;
              case 121: // y
                pressKey(writer, extendedVkey, 89);
                break;
              case 122: // z
                pressKey(writer, extendedVkey, 90);
                break;
              case 123: // {
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 93);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 125: // }
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 92);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              case 126: // ~
                pressKey(writer, 0|VK_SHIFT, XK_Shift_L);
                pressKey(writer, extendedVkey, 43);
                releaseKey(writer, 0|VK_SHIFT);
                break;
              default:
                pressKey(writer, extendedVkey, chars[i]);
                break;
            }
			break;
        }
      }
    }
    return;
  }

  // Either no chars were generated, or something outside the printable
  // character range.  Try ToAscii() without the Control and Alt keys down to
  // see if that yields an ordinary character.

  keystate[VK_CONTROL] = keystate[VK_LCONTROL] = keystate[VK_RCONTROL] = 0;
  keystate[VK_MENU] = keystate[VK_LMENU] = keystate[VK_RMENU] = 0;

  nchars = ToAscii(vkey, 0, keystate, (WORD*)&chars, 0);

  if (nchars < 0) {
    // So it would be a dead key if neither control nor alt were pressed.
    // However, we know that at least one of control and alt must be pressed.
    // We can't leave it at this stage otherwise the next call to ToAscii()
    // with a valid character will get wrongly interpreted in the context of
    // this bogus dead key.  Working on the assumption that a dead key followed
    // by space usually returns the dead character itself, try calling ToAscii
    // with VK_SPACE.
    vlog.debug("ToAscii dead key (2): extendedVkey 0x%x", extendedVkey);
    nchars = ToAscii(VK_SPACE, 0, keystate, (WORD*)&chars, 0);
    if (nchars < 0) {
      vlog.debug("ToAscii dead key (3): extendedVkey 0x%x - giving up!",
                 extendedVkey);
      return;
    }
  }

  if (nchars > 0 && IS_PRINTABLE_LATIN1(chars[0])) {
    for (int i = 0; i < nchars; i++) {
      vlog.debug("ToAscii key (2) (no ctrl/alt): extendedVkey 0x%x",
                 extendedVkey);
      if (chars[i] == 128) { // special hack for euro!
        pressKey(writer, extendedVkey, XK_EuroSign);
      } else {
         pressKey(writer, extendedVkey, chars[i]);
      }
    }
    return;
  }

  vlog.debug("no chars regardless of control and alt: extendedVkey 0x%x",
             extendedVkey);
}

// releaseAllKeys() - write key release events to the server for all keys
// that are currently regarded as being down.
void win32::CKeyboard::releaseAllKeys(InputHandler* writer) {
  std::map<int,rdr::U32>::iterator i, next_i;
  for (i=downKeysym.begin(); i!=downKeysym.end(); i=next_i) {
    next_i = i; next_i++;
    writer->keyEvent((*i).second, false);
    downKeysym.erase(i);
  }
}

// releaseKey() - write a key up event to the server, but only if we've
// actually sent a key down event for the given key.  The key up event always
// contains the same keysym we used in the key down event, regardless of what
// it would look up as using the current keyboard state.
void win32::CKeyboard::releaseKey(InputHandler* writer, int extendedVkey)
{
  if (downKeysym.find(extendedVkey) != downKeysym.end()) {
    vlog.debug("release extendedVkey 0x%x, keysym 0x%x",
               extendedVkey, downKeysym[extendedVkey]);
    writer->keyEvent(downKeysym[extendedVkey], false);
    downKeysym.erase(extendedVkey);
  }
}

// pressKey() - write a key down event to the server, and record which keysym
// was sent as corresponding to the given extendedVkey.  The only tricky bit is
// that if we are trying to press an extendedVkey which is already marked as
// down but with a different keysym, then we need to release the old keysym
// first.  This can happen in two cases: (a) when a single key press results in
// more than one character, and (b) when shift is released while another key is
// autorepeating.
void win32::CKeyboard::pressKey(InputHandler* writer, int extendedVkey,
                                rdr::U32 keysym)
{
  if (downKeysym.find(extendedVkey) != downKeysym.end()) {
    if (downKeysym[extendedVkey] != keysym) {
      vlog.debug("release extendedVkey 0x%x, keysym 0x%x",
                 extendedVkey, downKeysym[extendedVkey]);
      writer->keyEvent(downKeysym[extendedVkey], false);
    }
  }
  vlog.debug("press   extendedVkey 0x%x, keysym 0x%x",
             extendedVkey, keysym);
  writer->keyEvent(keysym, true);
  downKeysym[extendedVkey] = keysym;
}
