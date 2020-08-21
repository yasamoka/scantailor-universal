/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C)  Joseph Artsimovich <joseph.artsimovich@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ColorPickupInteraction.h"
#include "ZoneInteractionContext.h"
#include "InteractionState.h"
#include "ImageViewBase.h"
#include "PropertySet.h"
#include "ScopedIncDec.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include <QTransform>
#include <QCursor>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <Qt>
#include <vector>

namespace output
{

ColorPickupInteraction::ColorPickupInteraction(
    EditableZoneSet& zones, ZoneInteractionContext& context)
    :   m_rZones(zones),
        m_rContext(context),
        m_dontDrawCircle(0)
{
    m_interaction.setInteractionStatusTip(tr("Click on an area to pick up its color, or ESC to cancel."));
}

void
ColorPickupInteraction::startInteraction(
    EditableZoneSet::Zone const& zone, InteractionState& interaction)
{
    typedef FillColorProperty FCP;
    m_ptrFillColorProp = zone.properties()->locateOrCreate<FCP>();
    interaction.capture(m_interaction);
}

bool
ColorPickupInteraction::isActive(InteractionState const& interaction) const
{
    return interaction.capturedBy(m_interaction);
}

void
ColorPickupInteraction::onPaint(
    QPainter& painter, InteractionState const& interaction)
{
    if (m_dontDrawCircle) {
        return;
    }

    painter.setWorldTransform(QTransform());
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(Qt::red);
    pen.setWidthF(1.5);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(targetBoundingRect());
}

void
ColorPickupInteraction::onMousePressEvent(
    QMouseEvent* event, InteractionState& interaction)
{
    if (event->buttons() == Qt::LeftButton) { // Left and only left button.
        event->accept();
        takeColor();
        switchToDefaultInteraction();
    }
}

void
ColorPickupInteraction::onMouseMoveEvent(
    QMouseEvent* event, InteractionState& interaction)
{
    m_rContext.imageView().update();
}

void
ColorPickupInteraction::onKeyPressEvent(
    QKeyEvent* event, InteractionState& interaction)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        switchToDefaultInteraction();
    }
}

void
ColorPickupInteraction::takeColor()
{
    ScopedIncDec<int> const guard(m_dontDrawCircle);

    QRect const rect(targetBoundingRect());
    QPixmap const pixmap(QPixmap::grabWidget(&m_rContext.imageView(), rect));
    if (pixmap.isNull()) {
        return;
    }
    QImage const image(pixmap.toImage().convertToFormat(QImage::Format_RGB32));

    int const width = rect.width();
    int const height = rect.height();
    int const x_center = width / 2;
    int const y_center = height / 2;
    int const sqdist_threshold = x_center * y_center;
    uint32_t const* line = (uint32_t*)image.bits();
    int const stride = image.bytesPerLine() / 4;

    // We are going to take a median color using the bit-mixing technique.
    std::vector<uint32_t> bitmixed_colors;
    bitmixed_colors.reserve(width * height);

    // Take colors from the circle.
    for (int y = 0; y < height; ++y, line += stride) {
        int const dy = y - y_center;
        int const dy_sq = dy * dy;
        for (int x = 0; x < width; ++x) {
            int const dx = x - x_center;
            int const dx_sq = dx * dx;
            int const sqdist = dy_sq + dx_sq;
            if (sqdist <= sqdist_threshold) {
                uint32_t const color = line[x];
                bitmixed_colors.push_back(bitMixColor(color));
            }
        }
    }

    if (bitmixed_colors.empty()) {
        return;
    }

    std::vector<uint32_t>::iterator half_pos(bitmixed_colors.begin() + bitmixed_colors.size() / 2);
    std::nth_element(bitmixed_colors.begin(), half_pos, bitmixed_colors.end());
    QColor const color(bitUnmixColor(*half_pos));

    m_ptrFillColorProp->setColor(color);

    // Update default properties.
    PropertySet default_props(m_rZones.defaultProperties());
    default_props.locateOrCreate<FillColorProperty>()->setColor(color);
    m_rZones.setDefaultProperties(default_props);
    m_rZones.commit();
}

QRect
ColorPickupInteraction::targetBoundingRect() const
{
    QPoint const mouse_pos(m_rContext.imageView().mapFromGlobal(QCursor::pos()));
    QRect rect(0, 0, 15, 15); // Odd width and height are needed for symmetry.
    rect.moveCenter(mouse_pos);
    return rect;
}

void
ColorPickupInteraction::switchToDefaultInteraction()
{
    m_ptrFillColorProp.reset();
    m_interaction.release();
    makePeerPreceeder(*m_rContext.createDefaultInteraction());
    unlink();
    m_rContext.imageView().update();
}

uint32_t
ColorPickupInteraction::bitMixColor(uint32_t const color)
{
    return (
               m_sBitMixingLUT[0][(color >> 16) & 0xff] |
               m_sBitMixingLUT[1][(color >> 8) & 0xff] |
               m_sBitMixingLUT[2][color & 0xff]
           );
}

uint32_t
ColorPickupInteraction::bitUnmixColor(uint32_t const mixed)
{
    return (
               m_sBitUnmixingLUT[0][(mixed >> 16) & 0xff] |
               m_sBitUnmixingLUT[1][(mixed >> 8) & 0xff] |
               m_sBitUnmixingLUT[2][mixed & 0xff]
           );
}

/**
 * Generated like this:
 * \code
 * uint32_t const bit23 = 1 << 23;
 * int const bit7 = 1 << 7;
 * for (int partition = 0; partition < 3; ++partition) {
 *      for (int sample = 0; sample < 256; ++sample) {
 *          uint32_t accum = 0;
 *          for (int bit = 0; bit < 8; ++bit) {
 *              if (sample & (bit7 >> bit)) {
 *                  accum |= bit23 >> (bit * 3 + partition);
 *              }
 *          }
 *          m_sBitMixingLUT[partition][sample] = accum;
 *      }
 *  }
 * \endcode
 */
uint32_t const ColorPickupInteraction::m_sBitMixingLUT[3][256] = {
    {
        0x00000000, 0x00000004, 0x00000020, 0x00000024, 0x00000100, 0x00000104, 0x00000120, 0x00000124,
        0x00000800, 0x00000804, 0x00000820, 0x00000824, 0x00000900, 0x00000904, 0x00000920, 0x00000924,
        0x00004000, 0x00004004, 0x00004020, 0x00004024, 0x00004100, 0x00004104, 0x00004120, 0x00004124,
        0x00004800, 0x00004804, 0x00004820, 0x00004824, 0x00004900, 0x00004904, 0x00004920, 0x00004924,
        0x00020000, 0x00020004, 0x00020020, 0x00020024, 0x00020100, 0x00020104, 0x00020120, 0x00020124,
        0x00020800, 0x00020804, 0x00020820, 0x00020824, 0x00020900, 0x00020904, 0x00020920, 0x00020924,
        0x00024000, 0x00024004, 0x00024020, 0x00024024, 0x00024100, 0x00024104, 0x00024120, 0x00024124,
        0x00024800, 0x00024804, 0x00024820, 0x00024824, 0x00024900, 0x00024904, 0x00024920, 0x00024924,
        0x00100000, 0x00100004, 0x00100020, 0x00100024, 0x00100100, 0x00100104, 0x00100120, 0x00100124,
        0x00100800, 0x00100804, 0x00100820, 0x00100824, 0x00100900, 0x00100904, 0x00100920, 0x00100924,
        0x00104000, 0x00104004, 0x00104020, 0x00104024, 0x00104100, 0x00104104, 0x00104120, 0x00104124,
        0x00104800, 0x00104804, 0x00104820, 0x00104824, 0x00104900, 0x00104904, 0x00104920, 0x00104924,
        0x00120000, 0x00120004, 0x00120020, 0x00120024, 0x00120100, 0x00120104, 0x00120120, 0x00120124,
        0x00120800, 0x00120804, 0x00120820, 0x00120824, 0x00120900, 0x00120904, 0x00120920, 0x00120924,
        0x00124000, 0x00124004, 0x00124020, 0x00124024, 0x00124100, 0x00124104, 0x00124120, 0x00124124,
        0x00124800, 0x00124804, 0x00124820, 0x00124824, 0x00124900, 0x00124904, 0x00124920, 0x00124924,
        0x00800000, 0x00800004, 0x00800020, 0x00800024, 0x00800100, 0x00800104, 0x00800120, 0x00800124,
        0x00800800, 0x00800804, 0x00800820, 0x00800824, 0x00800900, 0x00800904, 0x00800920, 0x00800924,
        0x00804000, 0x00804004, 0x00804020, 0x00804024, 0x00804100, 0x00804104, 0x00804120, 0x00804124,
        0x00804800, 0x00804804, 0x00804820, 0x00804824, 0x00804900, 0x00804904, 0x00804920, 0x00804924,
        0x00820000, 0x00820004, 0x00820020, 0x00820024, 0x00820100, 0x00820104, 0x00820120, 0x00820124,
        0x00820800, 0x00820804, 0x00820820, 0x00820824, 0x00820900, 0x00820904, 0x00820920, 0x00820924,
        0x00824000, 0x00824004, 0x00824020, 0x00824024, 0x00824100, 0x00824104, 0x00824120, 0x00824124,
        0x00824800, 0x00824804, 0x00824820, 0x00824824, 0x00824900, 0x00824904, 0x00824920, 0x00824924,
        0x00900000, 0x00900004, 0x00900020, 0x00900024, 0x00900100, 0x00900104, 0x00900120, 0x00900124,
        0x00900800, 0x00900804, 0x00900820, 0x00900824, 0x00900900, 0x00900904, 0x00900920, 0x00900924,
        0x00904000, 0x00904004, 0x00904020, 0x00904024, 0x00904100, 0x00904104, 0x00904120, 0x00904124,
        0x00904800, 0x00904804, 0x00904820, 0x00904824, 0x00904900, 0x00904904, 0x00904920, 0x00904924,
        0x00920000, 0x00920004, 0x00920020, 0x00920024, 0x00920100, 0x00920104, 0x00920120, 0x00920124,
        0x00920800, 0x00920804, 0x00920820, 0x00920824, 0x00920900, 0x00920904, 0x00920920, 0x00920924,
        0x00924000, 0x00924004, 0x00924020, 0x00924024, 0x00924100, 0x00924104, 0x00924120, 0x00924124,
        0x00924800, 0x00924804, 0x00924820, 0x00924824, 0x00924900, 0x00924904, 0x00924920, 0x00924924
    },
    {
        0x00000000, 0x00000002, 0x00000010, 0x00000012, 0x00000080, 0x00000082, 0x00000090, 0x00000092,
        0x00000400, 0x00000402, 0x00000410, 0x00000412, 0x00000480, 0x00000482, 0x00000490, 0x00000492,
        0x00002000, 0x00002002, 0x00002010, 0x00002012, 0x00002080, 0x00002082, 0x00002090, 0x00002092,
        0x00002400, 0x00002402, 0x00002410, 0x00002412, 0x00002480, 0x00002482, 0x00002490, 0x00002492,
        0x00010000, 0x00010002, 0x00010010, 0x00010012, 0x00010080, 0x00010082, 0x00010090, 0x00010092,
        0x00010400, 0x00010402, 0x00010410, 0x00010412, 0x00010480, 0x00010482, 0x00010490, 0x00010492,
        0x00012000, 0x00012002, 0x00012010, 0x00012012, 0x00012080, 0x00012082, 0x00012090, 0x00012092,
        0x00012400, 0x00012402, 0x00012410, 0x00012412, 0x00012480, 0x00012482, 0x00012490, 0x00012492,
        0x00080000, 0x00080002, 0x00080010, 0x00080012, 0x00080080, 0x00080082, 0x00080090, 0x00080092,
        0x00080400, 0x00080402, 0x00080410, 0x00080412, 0x00080480, 0x00080482, 0x00080490, 0x00080492,
        0x00082000, 0x00082002, 0x00082010, 0x00082012, 0x00082080, 0x00082082, 0x00082090, 0x00082092,
        0x00082400, 0x00082402, 0x00082410, 0x00082412, 0x00082480, 0x00082482, 0x00082490, 0x00082492,
        0x00090000, 0x00090002, 0x00090010, 0x00090012, 0x00090080, 0x00090082, 0x00090090, 0x00090092,
        0x00090400, 0x00090402, 0x00090410, 0x00090412, 0x00090480, 0x00090482, 0x00090490, 0x00090492,
        0x00092000, 0x00092002, 0x00092010, 0x00092012, 0x00092080, 0x00092082, 0x00092090, 0x00092092,
        0x00092400, 0x00092402, 0x00092410, 0x00092412, 0x00092480, 0x00092482, 0x00092490, 0x00092492,
        0x00400000, 0x00400002, 0x00400010, 0x00400012, 0x00400080, 0x00400082, 0x00400090, 0x00400092,
        0x00400400, 0x00400402, 0x00400410, 0x00400412, 0x00400480, 0x00400482, 0x00400490, 0x00400492,
        0x00402000, 0x00402002, 0x00402010, 0x00402012, 0x00402080, 0x00402082, 0x00402090, 0x00402092,
        0x00402400, 0x00402402, 0x00402410, 0x00402412, 0x00402480, 0x00402482, 0x00402490, 0x00402492,
        0x00410000, 0x00410002, 0x00410010, 0x00410012, 0x00410080, 0x00410082, 0x00410090, 0x00410092,
        0x00410400, 0x00410402, 0x00410410, 0x00410412, 0x00410480, 0x00410482, 0x00410490, 0x00410492,
        0x00412000, 0x00412002, 0x00412010, 0x00412012, 0x00412080, 0x00412082, 0x00412090, 0x00412092,
        0x00412400, 0x00412402, 0x00412410, 0x00412412, 0x00412480, 0x00412482, 0x00412490, 0x00412492,
        0x00480000, 0x00480002, 0x00480010, 0x00480012, 0x00480080, 0x00480082, 0x00480090, 0x00480092,
        0x00480400, 0x00480402, 0x00480410, 0x00480412, 0x00480480, 0x00480482, 0x00480490, 0x00480492,
        0x00482000, 0x00482002, 0x00482010, 0x00482012, 0x00482080, 0x00482082, 0x00482090, 0x00482092,
        0x00482400, 0x00482402, 0x00482410, 0x00482412, 0x00482480, 0x00482482, 0x00482490, 0x00482492,
        0x00490000, 0x00490002, 0x00490010, 0x00490012, 0x00490080, 0x00490082, 0x00490090, 0x00490092,
        0x00490400, 0x00490402, 0x00490410, 0x00490412, 0x00490480, 0x00490482, 0x00490490, 0x00490492,
        0x00492000, 0x00492002, 0x00492010, 0x00492012, 0x00492080, 0x00492082, 0x00492090, 0x00492092,
        0x00492400, 0x00492402, 0x00492410, 0x00492412, 0x00492480, 0x00492482, 0x00492490, 0x00492492
    },
    {
        0x00000000, 0x00000001, 0x00000008, 0x00000009, 0x00000040, 0x00000041, 0x00000048, 0x00000049,
        0x00000200, 0x00000201, 0x00000208, 0x00000209, 0x00000240, 0x00000241, 0x00000248, 0x00000249,
        0x00001000, 0x00001001, 0x00001008, 0x00001009, 0x00001040, 0x00001041, 0x00001048, 0x00001049,
        0x00001200, 0x00001201, 0x00001208, 0x00001209, 0x00001240, 0x00001241, 0x00001248, 0x00001249,
        0x00008000, 0x00008001, 0x00008008, 0x00008009, 0x00008040, 0x00008041, 0x00008048, 0x00008049,
        0x00008200, 0x00008201, 0x00008208, 0x00008209, 0x00008240, 0x00008241, 0x00008248, 0x00008249,
        0x00009000, 0x00009001, 0x00009008, 0x00009009, 0x00009040, 0x00009041, 0x00009048, 0x00009049,
        0x00009200, 0x00009201, 0x00009208, 0x00009209, 0x00009240, 0x00009241, 0x00009248, 0x00009249,
        0x00040000, 0x00040001, 0x00040008, 0x00040009, 0x00040040, 0x00040041, 0x00040048, 0x00040049,
        0x00040200, 0x00040201, 0x00040208, 0x00040209, 0x00040240, 0x00040241, 0x00040248, 0x00040249,
        0x00041000, 0x00041001, 0x00041008, 0x00041009, 0x00041040, 0x00041041, 0x00041048, 0x00041049,
        0x00041200, 0x00041201, 0x00041208, 0x00041209, 0x00041240, 0x00041241, 0x00041248, 0x00041249,
        0x00048000, 0x00048001, 0x00048008, 0x00048009, 0x00048040, 0x00048041, 0x00048048, 0x00048049,
        0x00048200, 0x00048201, 0x00048208, 0x00048209, 0x00048240, 0x00048241, 0x00048248, 0x00048249,
        0x00049000, 0x00049001, 0x00049008, 0x00049009, 0x00049040, 0x00049041, 0x00049048, 0x00049049,
        0x00049200, 0x00049201, 0x00049208, 0x00049209, 0x00049240, 0x00049241, 0x00049248, 0x00049249,
        0x00200000, 0x00200001, 0x00200008, 0x00200009, 0x00200040, 0x00200041, 0x00200048, 0x00200049,
        0x00200200, 0x00200201, 0x00200208, 0x00200209, 0x00200240, 0x00200241, 0x00200248, 0x00200249,
        0x00201000, 0x00201001, 0x00201008, 0x00201009, 0x00201040, 0x00201041, 0x00201048, 0x00201049,
        0x00201200, 0x00201201, 0x00201208, 0x00201209, 0x00201240, 0x00201241, 0x00201248, 0x00201249,
        0x00208000, 0x00208001, 0x00208008, 0x00208009, 0x00208040, 0x00208041, 0x00208048, 0x00208049,
        0x00208200, 0x00208201, 0x00208208, 0x00208209, 0x00208240, 0x00208241, 0x00208248, 0x00208249,
        0x00209000, 0x00209001, 0x00209008, 0x00209009, 0x00209040, 0x00209041, 0x00209048, 0x00209049,
        0x00209200, 0x00209201, 0x00209208, 0x00209209, 0x00209240, 0x00209241, 0x00209248, 0x00209249,
        0x00240000, 0x00240001, 0x00240008, 0x00240009, 0x00240040, 0x00240041, 0x00240048, 0x00240049,
        0x00240200, 0x00240201, 0x00240208, 0x00240209, 0x00240240, 0x00240241, 0x00240248, 0x00240249,
        0x00241000, 0x00241001, 0x00241008, 0x00241009, 0x00241040, 0x00241041, 0x00241048, 0x00241049,
        0x00241200, 0x00241201, 0x00241208, 0x00241209, 0x00241240, 0x00241241, 0x00241248, 0x00241249,
        0x00248000, 0x00248001, 0x00248008, 0x00248009, 0x00248040, 0x00248041, 0x00248048, 0x00248049,
        0x00248200, 0x00248201, 0x00248208, 0x00248209, 0x00248240, 0x00248241, 0x00248248, 0x00248249,
        0x00249000, 0x00249001, 0x00249008, 0x00249009, 0x00249040, 0x00249041, 0x00249048, 0x00249049,
        0x00249200, 0x00249201, 0x00249208, 0x00249209, 0x00249240, 0x00249241, 0x00249248, 0x00249249
    }
};

/**
 * Generated like this:
 * \code
 * uint32_t const bit23 = 1 << 23;
 * int const bit7 = 1 << 7;
 * for (int partition = 0; partition < 3; ++partition) {
 *      for (int sample = 0; sample < 256; ++sample) {
 *          uint32_t accum = 0;
 *          for (int bit = 0; bit < 8; ++bit) {
 *              if (sample & (bit7 >> bit)) {
 *                  int const src_offset = bit + partition * 8;
 *                  int const channel = src_offset % 3;
 *                  int const dst_offset = src_offset / 3;
 *                  accum |= bit23 >> (channel * 8 + dst_offset);
 *              }
 *          }
 *          m_sBitUnmixingLUT[partition][sample] = accum;
 *      }
 *  }
 * \endcode
 */
uint32_t const ColorPickupInteraction::m_sBitUnmixingLUT[3][256] = {
    {
        0x00000000, 0x00002000, 0x00200000, 0x00202000, 0x00000040, 0x00002040, 0x00200040, 0x00202040,
        0x00004000, 0x00006000, 0x00204000, 0x00206000, 0x00004040, 0x00006040, 0x00204040, 0x00206040,
        0x00400000, 0x00402000, 0x00600000, 0x00602000, 0x00400040, 0x00402040, 0x00600040, 0x00602040,
        0x00404000, 0x00406000, 0x00604000, 0x00606000, 0x00404040, 0x00406040, 0x00604040, 0x00606040,
        0x00000080, 0x00002080, 0x00200080, 0x00202080, 0x000000c0, 0x000020c0, 0x002000c0, 0x002020c0,
        0x00004080, 0x00006080, 0x00204080, 0x00206080, 0x000040c0, 0x000060c0, 0x002040c0, 0x002060c0,
        0x00400080, 0x00402080, 0x00600080, 0x00602080, 0x004000c0, 0x004020c0, 0x006000c0, 0x006020c0,
        0x00404080, 0x00406080, 0x00604080, 0x00606080, 0x004040c0, 0x004060c0, 0x006040c0, 0x006060c0,
        0x00008000, 0x0000a000, 0x00208000, 0x0020a000, 0x00008040, 0x0000a040, 0x00208040, 0x0020a040,
        0x0000c000, 0x0000e000, 0x0020c000, 0x0020e000, 0x0000c040, 0x0000e040, 0x0020c040, 0x0020e040,
        0x00408000, 0x0040a000, 0x00608000, 0x0060a000, 0x00408040, 0x0040a040, 0x00608040, 0x0060a040,
        0x0040c000, 0x0040e000, 0x0060c000, 0x0060e000, 0x0040c040, 0x0040e040, 0x0060c040, 0x0060e040,
        0x00008080, 0x0000a080, 0x00208080, 0x0020a080, 0x000080c0, 0x0000a0c0, 0x002080c0, 0x0020a0c0,
        0x0000c080, 0x0000e080, 0x0020c080, 0x0020e080, 0x0000c0c0, 0x0000e0c0, 0x0020c0c0, 0x0020e0c0,
        0x00408080, 0x0040a080, 0x00608080, 0x0060a080, 0x004080c0, 0x0040a0c0, 0x006080c0, 0x0060a0c0,
        0x0040c080, 0x0040e080, 0x0060c080, 0x0060e080, 0x0040c0c0, 0x0040e0c0, 0x0060c0c0, 0x0060e0c0,
        0x00800000, 0x00802000, 0x00a00000, 0x00a02000, 0x00800040, 0x00802040, 0x00a00040, 0x00a02040,
        0x00804000, 0x00806000, 0x00a04000, 0x00a06000, 0x00804040, 0x00806040, 0x00a04040, 0x00a06040,
        0x00c00000, 0x00c02000, 0x00e00000, 0x00e02000, 0x00c00040, 0x00c02040, 0x00e00040, 0x00e02040,
        0x00c04000, 0x00c06000, 0x00e04000, 0x00e06000, 0x00c04040, 0x00c06040, 0x00e04040, 0x00e06040,
        0x00800080, 0x00802080, 0x00a00080, 0x00a02080, 0x008000c0, 0x008020c0, 0x00a000c0, 0x00a020c0,
        0x00804080, 0x00806080, 0x00a04080, 0x00a06080, 0x008040c0, 0x008060c0, 0x00a040c0, 0x00a060c0,
        0x00c00080, 0x00c02080, 0x00e00080, 0x00e02080, 0x00c000c0, 0x00c020c0, 0x00e000c0, 0x00e020c0,
        0x00c04080, 0x00c06080, 0x00e04080, 0x00e06080, 0x00c040c0, 0x00c060c0, 0x00e040c0, 0x00e060c0,
        0x00808000, 0x0080a000, 0x00a08000, 0x00a0a000, 0x00808040, 0x0080a040, 0x00a08040, 0x00a0a040,
        0x0080c000, 0x0080e000, 0x00a0c000, 0x00a0e000, 0x0080c040, 0x0080e040, 0x00a0c040, 0x00a0e040,
        0x00c08000, 0x00c0a000, 0x00e08000, 0x00e0a000, 0x00c08040, 0x00c0a040, 0x00e08040, 0x00e0a040,
        0x00c0c000, 0x00c0e000, 0x00e0c000, 0x00e0e000, 0x00c0c040, 0x00c0e040, 0x00e0c040, 0x00e0e040,
        0x00808080, 0x0080a080, 0x00a08080, 0x00a0a080, 0x008080c0, 0x0080a0c0, 0x00a080c0, 0x00a0a0c0,
        0x0080c080, 0x0080e080, 0x00a0c080, 0x00a0e080, 0x0080c0c0, 0x0080e0c0, 0x00a0c0c0, 0x00a0e0c0,
        0x00c08080, 0x00c0a080, 0x00e08080, 0x00e0a080, 0x00c080c0, 0x00c0a0c0, 0x00e080c0, 0x00e0a0c0,
        0x00c0c080, 0x00c0e080, 0x00e0c080, 0x00e0e080, 0x00c0c0c0, 0x00c0e0c0, 0x00e0c0c0, 0x00e0e0c0
    },
    {
        0x00000000, 0x00040000, 0x00000008, 0x00040008, 0x00000800, 0x00040800, 0x00000808, 0x00040808,
        0x00080000, 0x000c0000, 0x00080008, 0x000c0008, 0x00080800, 0x000c0800, 0x00080808, 0x000c0808,
        0x00000010, 0x00040010, 0x00000018, 0x00040018, 0x00000810, 0x00040810, 0x00000818, 0x00040818,
        0x00080010, 0x000c0010, 0x00080018, 0x000c0018, 0x00080810, 0x000c0810, 0x00080818, 0x000c0818,
        0x00001000, 0x00041000, 0x00001008, 0x00041008, 0x00001800, 0x00041800, 0x00001808, 0x00041808,
        0x00081000, 0x000c1000, 0x00081008, 0x000c1008, 0x00081800, 0x000c1800, 0x00081808, 0x000c1808,
        0x00001010, 0x00041010, 0x00001018, 0x00041018, 0x00001810, 0x00041810, 0x00001818, 0x00041818,
        0x00081010, 0x000c1010, 0x00081018, 0x000c1018, 0x00081810, 0x000c1810, 0x00081818, 0x000c1818,
        0x00100000, 0x00140000, 0x00100008, 0x00140008, 0x00100800, 0x00140800, 0x00100808, 0x00140808,
        0x00180000, 0x001c0000, 0x00180008, 0x001c0008, 0x00180800, 0x001c0800, 0x00180808, 0x001c0808,
        0x00100010, 0x00140010, 0x00100018, 0x00140018, 0x00100810, 0x00140810, 0x00100818, 0x00140818,
        0x00180010, 0x001c0010, 0x00180018, 0x001c0018, 0x00180810, 0x001c0810, 0x00180818, 0x001c0818,
        0x00101000, 0x00141000, 0x00101008, 0x00141008, 0x00101800, 0x00141800, 0x00101808, 0x00141808,
        0x00181000, 0x001c1000, 0x00181008, 0x001c1008, 0x00181800, 0x001c1800, 0x00181808, 0x001c1808,
        0x00101010, 0x00141010, 0x00101018, 0x00141018, 0x00101810, 0x00141810, 0x00101818, 0x00141818,
        0x00181010, 0x001c1010, 0x00181018, 0x001c1018, 0x00181810, 0x001c1810, 0x00181818, 0x001c1818,
        0x00000020, 0x00040020, 0x00000028, 0x00040028, 0x00000820, 0x00040820, 0x00000828, 0x00040828,
        0x00080020, 0x000c0020, 0x00080028, 0x000c0028, 0x00080820, 0x000c0820, 0x00080828, 0x000c0828,
        0x00000030, 0x00040030, 0x00000038, 0x00040038, 0x00000830, 0x00040830, 0x00000838, 0x00040838,
        0x00080030, 0x000c0030, 0x00080038, 0x000c0038, 0x00080830, 0x000c0830, 0x00080838, 0x000c0838,
        0x00001020, 0x00041020, 0x00001028, 0x00041028, 0x00001820, 0x00041820, 0x00001828, 0x00041828,
        0x00081020, 0x000c1020, 0x00081028, 0x000c1028, 0x00081820, 0x000c1820, 0x00081828, 0x000c1828,
        0x00001030, 0x00041030, 0x00001038, 0x00041038, 0x00001830, 0x00041830, 0x00001838, 0x00041838,
        0x00081030, 0x000c1030, 0x00081038, 0x000c1038, 0x00081830, 0x000c1830, 0x00081838, 0x000c1838,
        0x00100020, 0x00140020, 0x00100028, 0x00140028, 0x00100820, 0x00140820, 0x00100828, 0x00140828,
        0x00180020, 0x001c0020, 0x00180028, 0x001c0028, 0x00180820, 0x001c0820, 0x00180828, 0x001c0828,
        0x00100030, 0x00140030, 0x00100038, 0x00140038, 0x00100830, 0x00140830, 0x00100838, 0x00140838,
        0x00180030, 0x001c0030, 0x00180038, 0x001c0038, 0x00180830, 0x001c0830, 0x00180838, 0x001c0838,
        0x00101020, 0x00141020, 0x00101028, 0x00141028, 0x00101820, 0x00141820, 0x00101828, 0x00141828,
        0x00181020, 0x001c1020, 0x00181028, 0x001c1028, 0x00181820, 0x001c1820, 0x00181828, 0x001c1828,
        0x00101030, 0x00141030, 0x00101038, 0x00141038, 0x00101830, 0x00141830, 0x00101838, 0x00141838,
        0x00181030, 0x001c1030, 0x00181038, 0x001c1038, 0x00181830, 0x001c1830, 0x00181838, 0x001c1838
    },
    {
        0x00000000, 0x00000001, 0x00000100, 0x00000101, 0x00010000, 0x00010001, 0x00010100, 0x00010101,
        0x00000002, 0x00000003, 0x00000102, 0x00000103, 0x00010002, 0x00010003, 0x00010102, 0x00010103,
        0x00000200, 0x00000201, 0x00000300, 0x00000301, 0x00010200, 0x00010201, 0x00010300, 0x00010301,
        0x00000202, 0x00000203, 0x00000302, 0x00000303, 0x00010202, 0x00010203, 0x00010302, 0x00010303,
        0x00020000, 0x00020001, 0x00020100, 0x00020101, 0x00030000, 0x00030001, 0x00030100, 0x00030101,
        0x00020002, 0x00020003, 0x00020102, 0x00020103, 0x00030002, 0x00030003, 0x00030102, 0x00030103,
        0x00020200, 0x00020201, 0x00020300, 0x00020301, 0x00030200, 0x00030201, 0x00030300, 0x00030301,
        0x00020202, 0x00020203, 0x00020302, 0x00020303, 0x00030202, 0x00030203, 0x00030302, 0x00030303,
        0x00000004, 0x00000005, 0x00000104, 0x00000105, 0x00010004, 0x00010005, 0x00010104, 0x00010105,
        0x00000006, 0x00000007, 0x00000106, 0x00000107, 0x00010006, 0x00010007, 0x00010106, 0x00010107,
        0x00000204, 0x00000205, 0x00000304, 0x00000305, 0x00010204, 0x00010205, 0x00010304, 0x00010305,
        0x00000206, 0x00000207, 0x00000306, 0x00000307, 0x00010206, 0x00010207, 0x00010306, 0x00010307,
        0x00020004, 0x00020005, 0x00020104, 0x00020105, 0x00030004, 0x00030005, 0x00030104, 0x00030105,
        0x00020006, 0x00020007, 0x00020106, 0x00020107, 0x00030006, 0x00030007, 0x00030106, 0x00030107,
        0x00020204, 0x00020205, 0x00020304, 0x00020305, 0x00030204, 0x00030205, 0x00030304, 0x00030305,
        0x00020206, 0x00020207, 0x00020306, 0x00020307, 0x00030206, 0x00030207, 0x00030306, 0x00030307,
        0x00000400, 0x00000401, 0x00000500, 0x00000501, 0x00010400, 0x00010401, 0x00010500, 0x00010501,
        0x00000402, 0x00000403, 0x00000502, 0x00000503, 0x00010402, 0x00010403, 0x00010502, 0x00010503,
        0x00000600, 0x00000601, 0x00000700, 0x00000701, 0x00010600, 0x00010601, 0x00010700, 0x00010701,
        0x00000602, 0x00000603, 0x00000702, 0x00000703, 0x00010602, 0x00010603, 0x00010702, 0x00010703,
        0x00020400, 0x00020401, 0x00020500, 0x00020501, 0x00030400, 0x00030401, 0x00030500, 0x00030501,
        0x00020402, 0x00020403, 0x00020502, 0x00020503, 0x00030402, 0x00030403, 0x00030502, 0x00030503,
        0x00020600, 0x00020601, 0x00020700, 0x00020701, 0x00030600, 0x00030601, 0x00030700, 0x00030701,
        0x00020602, 0x00020603, 0x00020702, 0x00020703, 0x00030602, 0x00030603, 0x00030702, 0x00030703,
        0x00000404, 0x00000405, 0x00000504, 0x00000505, 0x00010404, 0x00010405, 0x00010504, 0x00010505,
        0x00000406, 0x00000407, 0x00000506, 0x00000507, 0x00010406, 0x00010407, 0x00010506, 0x00010507,
        0x00000604, 0x00000605, 0x00000704, 0x00000705, 0x00010604, 0x00010605, 0x00010704, 0x00010705,
        0x00000606, 0x00000607, 0x00000706, 0x00000707, 0x00010606, 0x00010607, 0x00010706, 0x00010707,
        0x00020404, 0x00020405, 0x00020504, 0x00020505, 0x00030404, 0x00030405, 0x00030504, 0x00030505,
        0x00020406, 0x00020407, 0x00020506, 0x00020507, 0x00030406, 0x00030407, 0x00030506, 0x00030507,
        0x00020604, 0x00020605, 0x00020704, 0x00020705, 0x00030604, 0x00030605, 0x00030704, 0x00030705,
        0x00020606, 0x00020607, 0x00020706, 0x00020707, 0x00030606, 0x00030607, 0x00030706, 0x00030707
    }
};

} // namespace output
