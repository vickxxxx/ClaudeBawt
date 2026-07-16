

#pragma once

namespace features {
    void InstallAll();
    void Tick();
    void GameTick();
}

namespace aim       { void Install(); void Tick(); }
namespace dodge     { void Install(); void Tick(); }
namespace speedhack { void Install(); void Tick(); float CurrentSpeed(); }
namespace loot      { void Install(); void Tick(); }
namespace glow      { void Install(); void Tick(); }
namespace fame      { void Install(); void Tick(); void Poll(); }
namespace noclip    { void Install(); void Tick(); void Poll(); bool GateActive();
                      void NoteMoveTarget(float x, float y); void SetManual(bool on); }
namespace hud       { void Tick(); }
namespace socketfu  { void Install(); void Tick(); }
namespace render_projectiles { void Install(); void Tick(); }
namespace render_tiles       { void Tick(); }
namespace render_hitbox      { void Tick(); }
namespace render_safety      { void Tick(); }
namespace render_units_grid  { void Tick(); }
namespace binds_overlay      { void Tick(); }
namespace notifications      { void Tick(); }
namespace interactive_map    { void Tick(); void Shutdown(); }
