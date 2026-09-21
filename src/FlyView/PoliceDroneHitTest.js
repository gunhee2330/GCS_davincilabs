.pragma library

/// The detected box under a point in panel pixels, as the detector's own rect (normalised in
/// the frame, which is what the tracker takes), or null. groups are tried in order, so persons
/// listed before vehicles win, and within a group a smaller box beats a larger one: a person
/// standing in front of a bus is picked, not the bus. Boxes smaller than touch pixels get the
/// touch size as hit area, since a person seen from 20 m is only a few pixels tall. content is
/// the VideoOutput's contentRect, so boxes cropped off the panel are skipped: they are not drawn.
function boxAt(groups, maxBoxes, content, width, height, touch, x, y) {
    for (const boxes of groups) {
        let hit = null
        for (let i = 0; i < Math.min(boxes.length, maxBoxes); ++i) {
            const b    = boxes[i]
            const left = content.x + b.x * content.width
            const top  = content.y + b.y * content.height
            const w    = b.width  * content.width
            const h    = b.height * content.height
            if (left + w <= 0 || top + h <= 0 || left >= width || top >= height) {
                continue
            }
            const slopX = Math.max(0, (touch - w) / 2)
            const slopY = Math.max(0, (touch - h) / 2)
            if (x < left - slopX || x > left + w + slopX || y < top - slopY || y > top + h + slopY) {
                continue
            }
            if (!hit || b.width * b.height < hit.width * hit.height) {
                hit = b
            }
        }
        if (hit) {
            return hit
        }
    }
    return null
}

/// The box a drag on the picture selected, as the tracker takes it: normalised in the frame, as
/// {left, top, right, bottom}. from and to are the press and the release point in panel pixels,
/// in either order, so a drag in any direction gives the same box. content is the VideoOutput's
/// contentRect, and the box is clamped to the part of it the panel actually shows, since
/// fullscreen on a non-16:9 screen crops the frame. Null when the picture is not on screen yet or
/// when a side of the box came out under minSide panel pixels: a flick is not a selection.
function dragBox(from, to, content, width, height, minSide) {
    const visLeft   = Math.max(content.x, 0)
    const visTop    = Math.max(content.y, 0)
    const visRight  = Math.min(content.x + content.width, width)
    const visBottom = Math.min(content.y + content.height, height)
    if ((visRight <= visLeft) || (visBottom <= visTop)) {
        return null
    }

    const left   = Math.max(visLeft,   Math.min(from.x, to.x))
    const top    = Math.max(visTop,    Math.min(from.y, to.y))
    const right  = Math.min(visRight,  Math.max(from.x, to.x))
    const bottom = Math.min(visBottom, Math.max(from.y, to.y))
    if ((right - left < minSide) || (bottom - top < minSide)) {
        return null
    }

    return {
        left:   (left   - content.x) / content.width,
        top:    (top    - content.y) / content.height,
        right:  (right  - content.x) / content.width,
        bottom: (bottom - content.y) / content.height
    }
}
