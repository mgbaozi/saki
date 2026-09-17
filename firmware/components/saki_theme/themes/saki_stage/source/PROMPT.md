# Saki Stage source prompt

The current source sheet was created on 2026-09-17 with OpenAI's built-in image generation tool.
The previous project-owned sheet was supplied only as a style and layout reference. No official
franchise image, logo, or extracted game/anime asset was supplied. The checked-in PNG and its
SHA-256 are the deterministic input to the firmware conversion step; image generation itself is
not expected to reproduce identical pixels.

Production prompt:

> Use the attached 3x3 sprite sheet only as a visual style and exact layout reference. Create a
> brand-new 3x3 status sprite sheet for a tiny 48x48 ESP32 display asset pack, 1536x1024 canvas,
> transparent background, no panel backgrounds, no written text, no logos, no watermark. Each cell
> must contain exactly one isolated full-body chibi character with thick dark pixel-art outlines,
> compact readable silhouette, crisp anime pixel-art rendering, and one large simple status prop or
> gesture. Keep generous transparent separation between cells. Do not merge cells.
>
> This is an unofficial fan-art mixed-cast sheet inspired by BanG Dream! It's MyGO!!!!! and Ave
> Mujica. Make the characters recognizable primarily through hair color, hairstyle, costume palette,
> instrument, and expression, while keeping the same unified original chibi pixel-art treatment as
> the reference.
>
> STRICT row-major cell mapping:
>
> 1. IDLE — Sakiko Togawa: icy light-blue hair, navy-and-gold formal stage outfit, quietly seated at
>    a tiny keyboard, calm.
> 2. STARTING — Sakiko Togawa: icy light-blue hair, navy-and-gold formal stage outfit, raising a
>    conductor baton, decisive start gesture.
> 3. THINKING — Sakiko Togawa: icy light-blue hair, hand at chin, one large question-mark thought
>    symbol.
> 4. WORKING — Taki Shiina: straight dark charcoal hair with cool purple undertone, intense focused
>    expression, actively striking a compact drum with two sticks, energetic motion lines.
> 5. WAITING_USER — Tomori Takamatsu: short fluffy ash-brown bob, soft brown eyes,
>    school-uniform-inspired gray and green palette, holding a small microphone close and reaching
>    one hand toward the viewer, timid expectant expression, one simple ellipsis speech bubble with
>    no text.
> 6. WAITING_APPROVAL — Soyo Nagasaki: long honey-blonde/light-brown hair, gentle but calculating
>    smile, green-accent school-uniform-inspired outfit, holding a clipboard close, one large golden
>    unlocked padlock beside her.
> 7. COMPLETED — Sakiko Togawa: icy light-blue hair, confident theatrical bow, golden laurel and
>    large teal check mark.
> 8. FAILED — Mutsumi Wakaba / Mortis: long mint-green hair, blank exhausted expression, gothic
>    black and dark-red Ave-Mujica-inspired stage outfit with red beret and green brooch, slumped
>    while holding a small green electric guitar, one large red X, a subtle split-persona shadow
>    silhouette behind her; this cell must be unmistakably different from Sakiko and is the most
>    important cell.
> 9. CANCELLED — Anon Chihaya: long pink hair in a fashionable side ponytail, pink-and-gray stage
>    outfit, turning away while closing a folder/case, one large red prohibited symbol.
>
> Keep the visual density and character scale consistent across all nine cells. Status symbols must
> remain visible after downscaling to 48x48. Avoid tiny details, gradients in the transparent
> background, extra people, duplicate limbs, extra instruments, any text, and cropping.

Failure-state edit prompt (Image 1 was the canonical sheet and Image 2 was an isolated intermediate
sprite created from the user-supplied pose/expression reference):

> Replace only the bottom-center cell 8 FAILED character and its effects with the isolated character
> and red X from Image 2. Scale and center the replacement so it fills the same visual footprint as
> the other cells and remains large and readable at 48×48. Remove every pixel of the old cell-8
> character, guitar, shadow, and any wheat/laurel spill from the neighboring completed cell inside
> cell 8. Preserve the 1536×1024 canvas, transparent RGBA background, 3×3 geometry, cell order, and
> cells 1–7 and 9. Cell 8 must contain only Mortis plus the red X, with clean transparent padding and
> no content crossing cell boundaries. Add no text, logo, watermark, panel, gradient, extra character,
> duplicated limb, or cropped hand.

The result is an unofficial fan derivative. Character names, likenesses, and referenced franchises
belong to their respective rights holders and are not licensed under Apache-2.0. See the pack README
and root NOTICE before redistribution.
