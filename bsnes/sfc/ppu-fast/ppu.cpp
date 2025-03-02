#include <sfc/sfc.hpp>

namespace SuperFamicom {

PPU& ppubase = ppu;

#define PPU PPUfast
#define ppu ppufast

PPU ppu;
#include "io.cpp"
#include "line.cpp"
#include "background.cpp"
#include "mode7.cpp"
#include "mode7hd.cpp"
#include "object.cpp"
#include "window.cpp"
#include "serialization.cpp"

auto PPU::interlace() const -> bool { return ppubase.display.interlace; }
auto PPU::overscan() const -> bool { return ppubase.display.overscan; }
auto PPU::vdisp() const -> uint { return ppubase.display.vdisp; }
auto PPU::hires() const -> bool { return latch.hires; }
auto PPU::hd() const -> bool { return latch.hd; }
auto PPU::ss() const -> bool { return latch.ss; }
#undef ppu
auto PPU::hdScale() const -> uint { return configuration.hacks.ppu.mode7.scale; }
auto PPU::hdPerspective() const -> uint { return configuration.hacks.ppu.mode7.perspective; }
auto PPU::hdSupersample() const -> uint { return configuration.hacks.ppu.mode7.supersample; }
auto PPU::hdMosaic() const -> uint { return configuration.hacks.ppu.mode7.mosaic; }
auto PPU::widescreen() const -> uint { return wsExt; }
auto PPU::widescreenRaw() const -> uint { return !hd() || configuration.hacks.ppu.mode7.wsMode == 0 ? 0 : configuration.hacks.ppu.mode7.widescreen; }
auto PPU::wsbg(uint bg) const -> uint {
  if (bg == Source::BG1) return configuration.hacks.ppu.mode7.wsbg1;
  if (bg == Source::BG2) return configuration.hacks.ppu.mode7.wsbg2;
  if (bg == Source::BG3) return configuration.hacks.ppu.mode7.wsbg3;
  if (bg == Source::BG4) return configuration.hacks.ppu.mode7.wsbg4;
  return 0; }
auto PPU::wsobj() const -> uint { return configuration.hacks.ppu.mode7.wsobj; }
auto PPU::winXad(uint x, bool bel) const -> uint {
  return ((configuration.hacks.ppu.mode7.igwin != 0 && (configuration.hacks.ppu.mode7.igwin >= 3
       || configuration.hacks.ppu.mode7.igwin >= 2 && ((bel ? io.col.window.belowMask : io.col.window.aboveMask) == 0)
       || configuration.hacks.ppu.mode7.igwin >= 1 && ((bel ? io.col.window.belowMask : io.col.window.aboveMask) == 2)))
    ? configuration.hacks.ppu.mode7.igwinx : x) + widescreen(); }
auto PPU::winXadHd(uint x, bool bel) const -> uint {
  return (configuration.hacks.ppu.mode7.igwin != 0 && (configuration.hacks.ppu.mode7.igwin >= 3
       || configuration.hacks.ppu.mode7.igwin >= 2 && ((bel ? io.col.window.belowMask : io.col.window.aboveMask) == 0)
       || configuration.hacks.ppu.mode7.igwin >= 1 && ((bel ? io.col.window.belowMask : io.col.window.aboveMask) == 2)))
    ? configuration.hacks.ppu.mode7.igwinx * PPU::hdScale() : x; }
auto PPU::strwin() const -> bool { return configuration.hacks.ppu.mode7.strwin; }
auto PPU::bgGrad() const -> uint { return !hd() ? 0 : configuration.hacks.ppu.mode7.bgGrad; }
auto PPU::windRad() const -> uint { return !hd() ? 0 : configuration.hacks.ppu.mode7.windRad; }
auto PPU::wsOverrideCandidate() const -> bool { return configuration.hacks.ppu.mode7.wsMode == 1; }
auto PPU::wsOverride() const -> bool { return mode7LineGroups.count < 1 && wsOverrideCandidate(); }
auto PPU::wsBgCol() const -> bool { return configuration.hacks.ppu.mode7.wsBgCol == 2
                                            || configuration.hacks.ppu.mode7.wsBgCol == 1 && wsOverride(); }
auto PPU::wsMarker() const -> uint { return configuration.hacks.ppu.mode7.wsMarker; }
auto PPU::wsMarkerAlpha() const -> uint { return configuration.hacks.ppu.mode7.wsMarkerAlpha; }
auto PPU::deinterlace() const -> bool { return configuration.hacks.ppu.deinterlace; }
auto PPU::renderCycle() const -> uint { return configuration.hacks.ppu.renderCycle; }
auto PPU::noVRAMBlocking() const -> bool { return configuration.hacks.ppu.noVRAMBlocking; }
#define ppu ppufast

PPU::PPU() {
  output = new uint32_t[256 * 61440]();
  for(uint y : range(240)) {
    lines[y].y = y;
  }
}

PPU::~PPU() {
  delete[] output;
  for(uint l : range(16)) delete[] lightTable[l];
}

auto PPU::synchronizeCPU() -> void {
  if(ppubase.clock >= 0) scheduler.resume(cpu.thread);
}

auto PPU::Enter() -> void {
  while(true) {
    scheduler.synchronize();
    ppu.main();
  }
}

auto PPU::step(uint clocks) -> void {
  tick(clocks);
  ppubase.clock += clocks;
  synchronizeCPU();
}

auto PPU::main() -> void {
  scanline();

  if(system.frameCounter == 0 && !system.runAhead) {
    uint y = vcounter();
    if(y >= 1 && y <= 239) {
      step(renderCycle());
      bool mosaicEnable = io.bg1.mosaicEnable || io.bg2.mosaicEnable || io.bg3.mosaicEnable || io.bg4.mosaicEnable;
      if(y == 1) {
        io.mosaic.counter = mosaicEnable ? io.mosaic.size + 1 : 0;
      }
      if(io.mosaic.counter && !--io.mosaic.counter) {
        io.mosaic.counter = mosaicEnable ? io.mosaic.size + 0 : 0;
      }
      lines[y].cache();
    }
  }

  step(hperiod() - hcounter());
}

auto PPU::scanline() -> void {
  if(vcounter() == 0) {
    if(latch.overscan && !io.overscan) {
      //when disabling overscan, clear the overscan area that won't be rendered to:
      for(uint y = 1; y <= 240; y++) {
        if(y >= 8 && y <= 231) continue;
        auto output = ppu.output + y * 1024;
        memory::fill<uint16>(output, 1024);
      }
    }

    ppubase.display.interlace = io.interlace;
    ppubase.display.overscan = io.overscan;
    latch.overscan = io.overscan;
    latch.hires = false;
    latch.hd = false;
    latch.ss = false;
    io.obj.timeOver = false;
    io.obj.rangeOver = false;
  }

  if(vcounter() > 0 && vcounter() < vdisp()) {
    latch.hires |= io.pseudoHires || io.bgMode == 5 || io.bgMode == 6;
    //supersampling and EXTBG mode are not compatible, so disable supersampling in EXTBG mode //HD:disabled
    latch.hd |= /* io.bgMode == 7 && */ hdScale() > 0 /*&& (hdSupersample() == 0*/ /* || io.extbg == 1 */ /*)*/; //HD:deactivated dynamic scale switching for widescreen
    latch.ss |= /* io.bgMode == 7 && */ hdScale() > 0 && (hdSupersample() > 1 /* && io.extbg == 0 */ ); //HD:irrelevant, because always used ORed with latch.hd
  }

  if(vcounter() == vdisp()) {
    if(!io.displayDisable) oamAddressReset();
  }

  if(vcounter() == 240) {
    Line::flush();
  }
}

auto PPU::refresh() -> void {
  if(system.frameCounter == 0 && !system.runAhead) {
    auto output = this->output;
    uint pitch, width, height;
    if(!hd()) {
      pitch  = 512 << !interlace();
      width  = 256 << hires();
      height = 240 << interlace();
    } else {
      pitch  = (256+2*widescreen()) * hdScale();
      width  = (256+2*widescreen()) * hdScale();
      height = 240 * hdScale();
    }

    //clear the areas of the screen that won't be rendered:
    //previous video frames may have drawn data here that would now be stale otherwise.
    if(!latch.overscan && pitch != frame.pitch && width != frame.width && height != frame.height) {
      for(uint y : range(240)) {
        if(y >= 8 && y <= 230) continue;  //these scanlines are always rendered.
        auto output = this->output + (!hd() ? (y * 1024 + (interlace() && field() ? 512 : 0)) : (y * 256 * hdScale() * hdScale()));
        auto width = (!hd() ? (!hires() ? 256 : 512) : (256 * hdScale() * hdScale()));
        memory::fill<uint32>(output, width);
      }
    }

    if(auto device = controllerPort2.device) device->draw(output, pitch * sizeof(uint32), width, height);
    platform->videoFrame(output, pitch * sizeof(uint32), width, height, hd() ? hdScale() : 1);

    frame.pitch  = pitch;
    frame.width  = width;
    frame.height = height;
  }
  if(system.frameCounter++ >= system.frameSkip) system.frameCounter = 0;
}

auto PPU::load() -> bool {
  return true;
}

auto PPU::power(bool reset) -> void {
  PPUcounter::reset();
  memory::fill<uint16>(output, 256 * 61440);

  function<uint8 (uint, uint8)> reader{&PPU::readIO, this};
  function<void  (uint, uint8)> writer{&PPU::writeIO, this};
  bus.map(reader, writer, "00-3f,80-bf:2100-213f");

  if(!reset) {
    for(auto& word : vram) word = 0x0000;
    for(auto& color : cgram) color = 0x0000;
    for(auto& object : objects) object = {};
  }

  latch = {};
  io = {};
  updateVideoMode();

  #undef ppu
  ItemLimit = !configuration.hacks.ppu.noSpriteLimit ? 32 : 128;
  TileLimit = !configuration.hacks.ppu.noSpriteLimit ? 34 : 128;

  Line::start = 0;
  Line::count = 0;

  frame = {};
}

}
