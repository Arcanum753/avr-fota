#include "e7rgb_matrix.h"

// ============================================================
// e7rgb_matrix.cpp — реализация драйвера матрицы.
// Развёртка координат — порт microLED getPixNumber():
//   conn = LEFT_BOTTOM(0) / LEFT_TOP(1) / RIGHT_TOP(2) / RIGHT_BOTTOM(3)
//   dir  = RIGHT(0) / UP(1) / LEFT(2) / DOWN(3)
//   config = conn | (dir << 2); матрицы вертикального хода
//   (config 4,13,14,7) считаются с длиной ряда = высота.
// ============================================================

// Соответствие «угла» (наш порядок: ВЛ/ВП/НЛ/НП) значению M_connection
static const uint8_t E7_CONN_OF_ORIGIN[4] = {
    1,   // E7_ORIGIN_TOP_LEFT    -> LEFT_TOP
    2,   // E7_ORIGIN_TOP_RIGHT   -> RIGHT_TOP
    0,   // E7_ORIGIN_BOTTOM_LEFT -> LEFT_BOTTOM
    3,   // E7_ORIGIN_BOTTOM_RIGHT-> RIGHT_BOTTOM
};

E7Matrix::E7Matrix() : _strip(NULL), _origin(E7_ORIGIN_BOTTOM_LEFT),
                       _direction(E7_DIR_UP), _layout(E7_LAYOUT_ZIGZAG) {}

void E7Matrix::setTopology(uint8_t origin, uint8_t direction, uint8_t layout) {
    if (origin    > 3) { origin    = E7_ORIGIN_BOTTOM_LEFT; }
    if (direction > 3) { direction = E7_DIR_UP; }
    if (layout    > 1) { layout    = E7_LAYOUT_ZIGZAG; }
    _origin = origin;
    _direction = direction;
    _layout = layout;
}

bool E7Matrix::ready() const {
    return (_strip != NULL);
}

void E7Matrix::init(int16_t dataPin) {
    destroy();
    if (dataPin < 0) { return; }
    _strip = new E7Strip(E7_LEDS, (uint8_t)dataPin);
    if (_strip == NULL) { return; }
    _strip->Begin();
    _strip->Show();
}

void E7Matrix::destroy() {
    if (_strip != NULL) {
        delete _strip;
        _strip = NULL;
    }
}

// Логические координаты (x 0..15 вправо, y 0..6 вверх) -> номер LED в ленте
uint16_t E7Matrix::ledIndex(int x, int y) {
    const int W = E7_WIDTH;
    const int H = E7_HEIGHT;
    uint8_t conn = E7_CONN_OF_ORIGIN[_origin];
    uint8_t config = conn | ((uint8_t)_direction << 2);

    uint16_t matrixW = W;
    if (config == 4 || config == 13 || config == 14 || config == 7) { matrixW = H; }

    int thisX, thisY;
    switch (config) {
        case 0:  thisX = x;                    thisY = y;            break;
        case 4:  thisX = y;                    thisY = x;            break;
        case 1:  thisX = x;                    thisY = (H - y - 1);  break;
        case 13: thisX = (H - y - 1);          thisY = x;            break;
        case 10: thisX = (W - x - 1);          thisY = (H - y - 1);  break;
        case 14: thisX = (H - y - 1);          thisY = (W - x - 1);  break;
        case 11: thisX = (W - x - 1);          thisY = y;            break;
        case 7:  thisX = y;                    thisY = (W - x - 1);  break;
        default: thisX = x;                    thisY = y;            break;  // защита
    }

    uint16_t idx;
    if (_layout == E7_LAYOUT_PARALLEL || !(thisY & 1)) {
        idx = (uint16_t)(thisY * matrixW + thisX);
    } else {
        idx = (uint16_t)(thisY * matrixW + (matrixW - thisX - 1));
    }
    if (idx >= E7_LEDS) { idx = 0; }
    return idx;
}

void E7Matrix::setPixel(int x, int y, uint32_t rgb, uint8_t brightness) {
    if (_strip == NULL) { return; }
    if (x < 0 || x >= E7_WIDTH || y < 0 || y >= E7_HEIGHT) { return; }
    uint16_t idx = ledIndex(x, y);
    float b = brightness / 255.0f;
    uint16_t r = (uint16_t)(((rgb >> 16) & 0xFF) * b);
    uint16_t g = (uint16_t)(((rgb >> 8)  & 0xFF) * b);
    uint16_t bl = (uint16_t)((rgb & 0xFF) * b);
    _strip->SetPixelColor(idx, RgbColor(r, g, bl));
}

void E7Matrix::clear() {
    if (_strip == NULL) { return; }
    _strip->ClearTo(RgbColor(0, 0, 0));
}

void E7Matrix::show() {
    if (_strip == NULL) { return; }
    _strip->Show();
}
