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
