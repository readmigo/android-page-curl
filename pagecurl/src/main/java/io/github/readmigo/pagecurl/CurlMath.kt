package io.github.readmigo.pagecurl

import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Path
import android.graphics.PointF
import android.graphics.Shader
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.sqrt

/**
 * Computed geometry for a single frame of page curl animation.
 *
 * Describes the fold line, clipping regions, reflection matrix, and
 * shadow gradients needed to render the curl on a Canvas.
 */
internal class CurlFrame(
    /** Clip path for the flat (uncurled) region of the current page. */
    val flatPath: Path,
    /** Clip path for the back-face (reflected curl) region. */
    val backPath: Path,
    /** Matrix that reflects content across the fold line (for back face). */
    val backMatrix: Matrix,
    /** Shadow gradient cast by the curled page onto the revealed page. */
    val castShadowGradient: LinearGradient?,
    /** Shadow gradient at the fold crease on the flat page. */
    val creaseShadowGradient: LinearGradient?,
    /** Region where the cast shadow is drawn (strip on curl side of fold). */
    val shadowRegionPath: Path,
    /** Region where the crease shadow is drawn (strip on flat side of fold). */
    val creaseRegionPath: Path,
    /** Narrow strip along the fold line representing the visible curl cylinder. */
    val curlStripPath: Path,
    /** Gradient overlay on the curl strip for 3D cylinder illusion. */
    val curlHighlightGradient: LinearGradient?,
    /** Whether a curl is actually visible (false = page is fully flat). */
    val isVisible: Boolean
)

/**
 * Pure-math geometry calculator for the bezier page curl effect.
 *
 * Given the dragged corner position (touch) and the original corner position,
 * computes fold line, clipping regions, reflection matrix, and shadow parameters.
 *
 * The fold line is the perpendicular bisector of the segment from touch to corner.
 * Everything on the corner's side of this line is the "curl region" (back face).
 * Everything on the other side is the "flat region" (front face, still lying flat).
 */
internal object CurlMath {

    private const val CAST_SHADOW_FRACTION = 0.15f   // shadow width as fraction of page width
    private const val CREASE_SHADOW_FRACTION = 0.06f  // crease width as fraction of page width
    private const val CAST_SHADOW_ALPHA = 80          // max shadow opacity (0-255)
    private const val CREASE_SHADOW_ALPHA = 50        // max crease opacity (0-255)
    private const val CURL_STRIP_FRACTION = 0.08f     // curl cylinder width as fraction of page width

    /**
     * Calculate the complete curl geometry for one frame.
     *
     * @param touch   Where the page corner has been dragged to (in page coordinates).
     * @param corner  Original position of the page corner (e.g. pageW, pageH).
     * @param pageW   Page width in pixels.
     * @param pageH   Page height in pixels.
     * @return A [CurlFrame] with all data needed to render this frame.
     */
    fun calculate(
        touch: PointF,
        corner: PointF,
        pageW: Float,
        pageH: Float
    ): CurlFrame {
        // Vector from touch to corner
        val dx = corner.x - touch.x
        val dy = corner.y - touch.y
        val dist = sqrt(dx * dx + dy * dy)

        if (dist < 1f) {
            return noCurl(pageW, pageH)
        }

        // Midpoint of touch-to-corner segment (point on the fold line)
        val mx = (touch.x + corner.x) / 2f
        val my = (touch.y + corner.y) / 2f

        // Unit normal from fold line toward corner (curl side)
        val nx = dx / dist
        val ny = dy / dist

        // Fold line direction (perpendicular to normal)
        val fldx = -ny
        val fldy = nx

        // Extend fold line far beyond page for intersection calculations
        val ext = (pageW + pageH) * 2f
        val foldP1 = PointF(mx - fldx * ext, my - fldy * ext)
        val foldP2 = PointF(mx + fldx * ext, my + fldy * ext)

        // Classify point: flat side (toward touch) vs curl side (toward corner)
        fun isFlat(px: Float, py: Float): Boolean {
            return (px - mx) * nx + (py - my) * ny <= 0
        }

        // Build the two region polygons
        val (flatPath, curlPath) = buildRegionPaths(
            foldP1, foldP2, pageW, pageH, ::isFlat
        )

        if (curlPath.isEmpty) {
            return noCurl(pageW, pageH)
        }

        // Reflection matrix across fold line (for drawing the back face)
        val backMatrix = buildReflectionMatrix(mx, my, fldx, fldy)

        // Shadow parameters scale with page size
        val castShadowW = pageW * CAST_SHADOW_FRACTION
        val creaseShadowW = pageW * CREASE_SHADOW_FRACTION

        // Cast shadow: dark at fold line, fading toward curl side
        val castShadowGradient = LinearGradient(
            mx, my,
            mx + nx * castShadowW, my + ny * castShadowW,
            intArrayOf(
                android.graphics.Color.argb(CAST_SHADOW_ALPHA, 0, 0, 0),
                android.graphics.Color.argb(0, 0, 0, 0)
            ),
            null,
            Shader.TileMode.CLAMP
        )

        // Crease shadow: subtle dark at fold line, fading toward flat side
        val creaseShadowGradient = LinearGradient(
            mx, my,
            mx - nx * creaseShadowW, my - ny * creaseShadowW,
            intArrayOf(
                android.graphics.Color.argb(CREASE_SHADOW_ALPHA, 0, 0, 0),
                android.graphics.Color.argb(0, 0, 0, 0)
            ),
            null,
            Shader.TileMode.CLAMP
        )

        // Shadow strip region on curl side of fold
        val shadowRegion = buildShadowStrip(
            mx, my, nx, ny, fldx, fldy, castShadowW, pageW, pageH
        )

        // Crease strip region on flat side of fold
        val creaseRegion = buildShadowStrip(
            mx, my, -nx, -ny, fldx, fldy, creaseShadowW, pageW, pageH
        )

        // Curl strip: narrow band on curl side representing the visible curl cylinder
        val curlStripW = pageW * CURL_STRIP_FRACTION
        val curlStripPath = buildShadowStrip(
            mx, my, nx, ny, fldx, fldy, curlStripW, pageW, pageH
        )

        // Highlight gradient for 3D cylinder illusion on the curl strip
        val curlHighlightGradient = LinearGradient(
            mx, my,
            mx + nx * curlStripW, my + ny * curlStripW,
            intArrayOf(
                android.graphics.Color.argb(60, 255, 255, 255),  // bright at fold edge
                android.graphics.Color.argb(0, 128, 128, 128),   // neutral center
                android.graphics.Color.argb(80, 0, 0, 0)         // shadow at outer edge
            ),
            floatArrayOf(0f, 0.35f, 1f),
            Shader.TileMode.CLAMP
        )

        return CurlFrame(
            flatPath = flatPath,
            backPath = curlPath,
            backMatrix = backMatrix,
            castShadowGradient = castShadowGradient,
            creaseShadowGradient = creaseShadowGradient,
            shadowRegionPath = shadowRegion,
            creaseRegionPath = creaseRegion,
            curlStripPath = curlStripPath,
            curlHighlightGradient = curlHighlightGradient,
            isVisible = true
        )
    }

    /** Returns a CurlFrame representing a fully flat page (no curl). */
    private fun noCurl(pageW: Float, pageH: Float): CurlFrame {
        val full = Path().apply { addRect(0f, 0f, pageW, pageH, Path.Direction.CW) }
        return CurlFrame(
            flatPath = full,
            backPath = Path(),
            backMatrix = Matrix(),
            castShadowGradient = null,
            creaseShadowGradient = null,
            shadowRegionPath = Path(),
            creaseRegionPath = Path(),
            curlStripPath = Path(),
            curlHighlightGradient = null,
            isVisible = false
        )
    }

    // -----------------------------------------------------------------------
    // Region polygon construction
    // -----------------------------------------------------------------------

    /**
     * Split the page rectangle into two polygons along the fold line.
     *
     * Walks the page corners clockwise (TL → TR → BR → BL). When the fold
     * line crosses an edge (i.e., consecutive corners are on different sides),
     * the intersection point is inserted into both polygons.
     */
    private fun buildRegionPaths(
        foldP1: PointF,
        foldP2: PointF,
        pageW: Float,
        pageH: Float,
        isFlat: (Float, Float) -> Boolean
    ): Pair<Path, Path> {
        val corners = arrayOf(
            PointF(0f, 0f),       // TL
            PointF(pageW, 0f),    // TR
            PointF(pageW, pageH), // BR
            PointF(0f, pageH)     // BL
        )

        val flatVerts = mutableListOf<PointF>()
        val curlVerts = mutableListOf<PointF>()

        for (i in corners.indices) {
            val c1 = corners[i]
            val c2 = corners[(i + 1) % 4]
            val c1Flat = isFlat(c1.x, c1.y)
            val c2Flat = isFlat(c2.x, c2.y)

            // Add the current corner to its side
            if (c1Flat) flatVerts.add(c1) else curlVerts.add(c1)

            // If the fold line crosses this edge, insert the intersection
            if (c1Flat != c2Flat) {
                lineSegmentIntersection(foldP1, foldP2, c1, c2)?.let {
                    flatVerts.add(it)
                    curlVerts.add(it)
                }
            }
        }

        return Pair(pointsToPath(flatVerts), pointsToPath(curlVerts))
    }

    private fun pointsToPath(points: List<PointF>): Path {
        val path = Path()
        if (points.size < 3) return path
        path.moveTo(points[0].x, points[0].y)
        for (i in 1 until points.size) {
            path.lineTo(points[i].x, points[i].y)
        }
        path.close()
        return path
    }

    // -----------------------------------------------------------------------
    // Line / segment intersection
    // -----------------------------------------------------------------------

    /**
     * Intersect an infinite line (through p1, p2) with a finite segment (p3→p4).
     * Returns the intersection point if it lies on the segment, null otherwise.
     */
    private fun lineSegmentIntersection(
        p1: PointF, p2: PointF,
        p3: PointF, p4: PointF
    ): PointF? {
        val d1x = p2.x - p1.x
        val d1y = p2.y - p1.y
        val d2x = p4.x - p3.x
        val d2y = p4.y - p3.y

        val denom = d1x * d2y - d1y * d2x
        if (abs(denom) < 0.001f) return null // parallel

        // Parameter u is for the segment p3→p4; must be in [0, 1]
        val u = ((p3.x - p1.x) * d1y - (p3.y - p1.y) * d1x) / denom
        if (u < -0.001f || u > 1.001f) return null

        val uc = u.coerceIn(0f, 1f)
        return PointF(p3.x + uc * d2x, p3.y + uc * d2y)
    }

    // -----------------------------------------------------------------------
    // Reflection matrix
    // -----------------------------------------------------------------------

    /**
     * Build a 2D reflection matrix across a line through (mx, my)
     * with direction (fldx, fldy).
     *
     * Formula: Translate(mx,my) * ReflectAboutOrigin(ux,uy) * Translate(-mx,-my)
     */
    private fun buildReflectionMatrix(
        mx: Float, my: Float,
        fldx: Float, fldy: Float
    ): Matrix {
        val len = sqrt(fldx * fldx + fldy * fldy)
        if (len < 0.001f) return Matrix()
        val ux = fldx / len
        val uy = fldy / len

        // Reflection across line through origin with unit direction (ux, uy):
        //   | 2*ux²-1    2*ux*uy   0 |
        //   | 2*ux*uy    2*uy²-1   0 |
        //   | 0           0        1 |
        // Combined with translate(-mx,-my) before and translate(mx,my) after:
        val a = 2f * ux * ux - 1f
        val b = 2f * ux * uy
        val d = 2f * uy * uy - 1f
        val tx = mx - a * mx - b * my
        val ty = my - b * mx - d * my

        return Matrix().apply {
            setValues(
                floatArrayOf(
                    a, b, tx,
                    b, d, ty,
                    0f, 0f, 1f
                )
            )
        }
    }

    // -----------------------------------------------------------------------
    // Shadow strip geometry
    // -----------------------------------------------------------------------

    /**
     * Build a parallelogram strip along the fold line, offset in the
     * given normal direction by [width] pixels.
     */
    private fun buildShadowStrip(
        mx: Float, my: Float,
        nx: Float, ny: Float,         // offset direction
        fldx: Float, fldy: Float,     // fold line direction
        width: Float,
        pageW: Float, pageH: Float
    ): Path {
        val ext = max(pageW, pageH) * 2f
        val path = Path()
        // Parallelogram: fold line → offset by width in normal direction
        path.moveTo(mx - fldx * ext, my - fldy * ext)
        path.lineTo(mx + fldx * ext, my + fldy * ext)
        path.lineTo(mx + fldx * ext + nx * width, my + fldy * ext + ny * width)
        path.lineTo(mx - fldx * ext + nx * width, my - fldy * ext + ny * width)
        path.close()

        // Clip to page bounds
        val pagePath = Path()
        pagePath.addRect(0f, 0f, pageW, pageH, Path.Direction.CW)
        path.op(pagePath, Path.Op.INTERSECT)

        return path
    }
}
