# Changelog

## 1.0.4

- Added native XInput fallback for controllers unavailable through Steam Input.

## 1.0.3

- Reworked camouflage swaps around the game's actor scheduler, keeping Snake
  paused until the full model change completes. This fixes repeat-swap and
  transition crashes.
- Matched native handling for Tuxedo, Mask, and face-paint-only changes.
- Added left-stick menu navigation alongside the D-pad.
- Added crash dumps and focused swap-state logging for future diagnostics.

## 1.0.2

- Fixed overlay positioning when internal render resolution differs from the
  output resolution.
