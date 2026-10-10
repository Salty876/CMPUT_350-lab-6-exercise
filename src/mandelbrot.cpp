#include "mandelbrot.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <sstream>

#include "cyclic_gradient.h"
#include "open_sans_semibold.h"
#include "vector_util.h"

// For use by our constructor.
// Returns the appropriate initial world bounds.
// Should always contain the coordinates bounded in (-2.5, -1.5) to (0.5, 1.5).
std::pair<sf::Vector2<double>, sf::Vector2<double>>
MandelbrotViewer::getInitialWorldBoundsForWindowSize(unsigned int windowWidth,
                                                     unsigned int windowHeight) {
    assert(windowWidth > 0);
    assert(windowHeight > 0);
    const double aspectRatio = static_cast<double>(windowWidth) / windowHeight;
    const sf::Vector2<double> worldCenter{-1., 0.};
    sf::Vector2<double> initWorldSize = {3., 3.};
    if (windowWidth >= windowHeight) {
        // world height will be 3
        initWorldSize.x = 3 * aspectRatio;
    } else {
        // world width will be 3
        initWorldSize.y = 3 / aspectRatio;
    }
    return {worldCenter - initWorldSize / 2., worldCenter + initWorldSize / 2.};
}

MandelbrotViewer::MandelbrotViewer(unsigned int windowWidth, unsigned int windowHeight)
    : mWindow(sf::VideoMode(sf::Vector2u{windowWidth, windowHeight}), "Mandelbrot set viewer"),
      mWindowSize(windowWidth, windowHeight),
      mFont(EmbeddedFonts::OpenSans_SemiBold_ttf, EmbeddedFonts::OpenSans_SemiBold_ttf_len),
      mCursorWorldPosText(mFont),
      mCursorWorldPosTextShadow(mFont),
      mViewBuffer(sf::Vector2u{windowWidth, windowHeight}, sf::Color::Blue),
      mViewBufferGPU(mViewBuffer),
      mViewSprite(mViewBufferGPU) {
    assert(windowWidth > 0);
    assert(windowHeight > 0);
    std::tie(mMinPointWorld, mMaxPointWorld) =
        getInitialWorldBoundsForWindowSize(windowWidth, windowHeight);

    // disable key repeat
    mWindow.setKeyRepeatEnabled(false);

    // Initially no text to show
    mCursorWorldPosText.setString("");
    mCursorWorldPosTextShadow.setString("");
    // Character size of 14 pixels
    mCursorWorldPosText.setCharacterSize(14);
    mCursorWorldPosTextShadow.setCharacterSize(14);
    // Text is white, shadow is black.
    mCursorWorldPosText.setFillColor(sf::Color::White);
    mCursorWorldPosTextShadow.setFillColor(sf::Color(0, 0, 0, 130));
    // Set text position
    mCursorWorldPosText.setPosition({10, 10});
    mCursorWorldPosTextShadow.setPosition({10, 10});
    // Give text shadow extra thickness.
    mCursorWorldPosTextShadow.setOutlineColor(sf::Color(0, 0, 0, 130));
    mCursorWorldPosTextShadow.setOutlineThickness(3);
}

void MandelbrotViewer::run() {
    sf::Clock clock;
    sf::Time lastTime = sf::Time();
    int maxIters = MAX_ITERS_LOWER_BOUND;
    while (true) {
        sf::Time curTime = clock.getElapsedTime();
        // Handle inputs
        InputSummary inputSummary = readInputs();
        if (inputSummary.shouldClose) {
            mWindow.close();
            return;
        }
        // Determine whether any of the inputs cause the view to change.
        bool viewChanged = (inputSummary.zoomDistance != 0) ||
                           (inputSummary.panVector != sf::Vector2<double>{0., 0.}) ||
                           inputSummary.shouldResize;
        // Update
        updateViewState(inputSummary, curTime - lastTime);
        updateUIText(inputSummary.mousePosition);
        maxIters = (viewChanged ? MAX_ITERS_LOWER_BOUND
                                : std::min(maxIters * ITERS_MULTIPLIER, MAX_ITERS_UPPER_BOUND));
        // Render:
        drawIntoViewBuffer(maxIters);
        copyViewBufferToGPU();
        draw();
        // update timing stats
        lastTime = curTime;
    }
}

MandelbrotViewer::InputSummary MandelbrotViewer::readInputs() {
    InputSummary ret{};
    while (const std::optional<sf::Event> event = mWindow.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            ret.shouldClose = true;
            return ret;  // just stop early
        } else if (const auto* mouseScrolled = event->getIf<sf::Event::MouseWheelScrolled>()) {
            // If multiple of these, it adds up.
            ret.zoomDistance += mouseScrolled->delta;
        } else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            ret.desiredWindowSize = resized->size;
            ret.shouldResize = true;
        }
    }
    // pan is a continuous action, so we'll just check current state for each key. Note, NOT
    // else if.
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::W)) {
        ret.panVector += {0., 1};
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::S)) {
        ret.panVector += {0., -1.};
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::A)) {
        ret.panVector += {-1., 0.};
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::D)) {
        ret.panVector += {1., 0.};
    }
    ret.mousePosition = sf::Mouse::getPosition(mWindow);
    return ret;
}

void MandelbrotViewer::updateViewState(const InputSummary& inputs, sf::Time deltaTime) {
    if (inputs.shouldResize) {
        handleWindowResize(inputs.desiredWindowSize);
    }
    if (inputs.zoomDistance != 0) {
        handleZoom(inputs.zoomDistance, inputs.mousePosition);
    }
    if (inputs.panVector != sf::Vector2<double>{0., 0.}) {
        handlePans(inputs.panVector, deltaTime);
    }
}

void MandelbrotViewer::handleZoom(double scrollDistance, sf::Vector2i mousePosition) {
    double worldViewFactor = std::pow(ZOOM_EXPONENT_BASE, scrollDistance);

    // TODO: expand our world view bounds (mMinPointWorld, mMaxPointWorld)
    // by worldViewFactor around the current world point being pointed to by the user's cursor.
    // In particular, the new world-coordinates rectangle will be of size
    // (worldViewFactor * (orig world width), worldViewFactor * (orig world height)),
    // and the user's cursor will point to exactly the same thing before and after the zoom.

    float windowWidth = mWindow.getSize().x;
    float windowHeight = mWindow.getSize().y;

    sf::Vector2<double> worldSize = mMaxPointWorld - mMinPointWorld;

    double mouseX = mousePosition.x / windowWidth;
    double mouseY = mousePosition.y / windowHeight;

    sf::Vector2<double> mouseWorld(mMinPointWorld.x + mouseX * worldSize.x, mMinPointWorld.y + mouseY * worldSize.y);

    mMinPointWorld = mouseWorld + (mMinPointWorld - mouseWorld) * worldViewFactor;
    mMaxPointWorld = mouseWorld + (mMaxPointWorld - mouseWorld) * worldViewFactor;

}

void MandelbrotViewer::handleWindowResize(sf::Vector2u newSize)  // newSize is in window coords.
{
    // IMPORTANT: after resizing window, need to fix view, or else original world
    // rectangle gets scaled to new window size instead. This line ensures that when the window
    // expands, "more of the world" is correspondingly viewable.
    mWindow.setView(sf::View(
        sf::FloatRect({0, 0}, {static_cast<float>(newSize.x), static_cast<float>(newSize.y)})));

    // TODO: handle window resizes. In particular, update mMinPointWorld and mMaxPointWorld
    //       such that the world view is the same aspect ratio as the new window size (such that
    //       the world view is not distorted), and centered around the same world point they used to
    //       be. The rectangle's size in each dimension is scaled by the same factor as the window
    //       was scaled in the respective dimension, such that the original view is only
    //       cropped/extended, not zoomed.
    // ... your code here...
    float scaleX = static_cast<float>(newSize.x) / mWindowSize.x;
    float scaleY = static_cast<float>(newSize.y) / mWindowSize.y;

    sf::Vector2<double> centerWorld = (mMaxPointWorld + mMinPointWorld) * 0.5;

    sf::Vector2<double> currWorldSize = mMaxPointWorld - mMinPointWorld;

    currWorldSize.x *= scaleX;
    currWorldSize.y *= scaleY;

    mMinPointWorld = centerWorld - 0.5 * currWorldSize;
    mMaxPointWorld = centerWorld + 0.5 * currWorldSize;




    // update CPU-side image buffer size to have enough memory for all the pixels:
    mViewBuffer.resize(newSize);
    // TODO: update mViewBufferGPU so that it has enough memory for all the pixels in the new window
    // size
    //      Hint: (void)mViewBufferGPU.resize ... something ... this is a trivial one-liner.
    mViewBufferGPU.resize(newSize);
    // The sprite will have an incorrect view into the texture after resize, so we update:
    mViewSprite.setTextureRect(sf::IntRect({0, 0}, sf::Vector2i(newSize)));
    mWindowSize = newSize;  // update mWindowSize.
}

void MandelbrotViewer::handlePans(sf::Vector2<double> basePanVector, sf::Time deltaTime) {
    const sf::Vector2<double> worldViewSize = mMaxPointWorld - mMinPointWorld;
    const double panScale = (worldViewSize.x + worldViewSize.y) * 0.5;
    const auto panDelta =
        basePanVector * static_cast<double>(deltaTime.asMilliseconds()) * panScale * PAN_FACTOR;
    mMinPointWorld += panDelta;
    mMaxPointWorld += panDelta;
}

std::string MandelbrotViewer::formatCoords(const sf::Vector2<double>& pos) {
    std::stringstream ss;
    ss << std::setprecision(15) << "(" << pos.x << ", " << pos.y << ")";
    return ss.str();
}

// updateUIText updates mCursorWorldPosText and mCursorWorldPosTextShadow
// to show the cursor's current **world coordinates** position
void MandelbrotViewer::updateUIText(sf::Vector2i mouseWindowCoords) {
    auto coordsString = formatCoords(windowPosToWorld(sf::Vector2<double>(mouseWindowCoords)));
    mCursorWorldPosText.setString(coordsString);
    mCursorWorldPosTextShadow.setString(coordsString);
}

double MandelbrotViewer::mandelbrot(double cX, double cY, int maxIters) const {
    // TODO: return the number of iterations it takes for z to escape a radius of 2,
    //       if it happens within maxIters iterations, otherwise return infinity.

    int currIters = 0;

    double zX = 0.0;
    double zY = 0.0;

    double zPrimeX = 0.0;
    double zPrimeY = 0.0;

    while (currIters < maxIters) {
        zPrimeX = zX * zX - zY * zY + cX;
        zPrimeY = 2.0 * zX * zY + cY;

        float norm = std::sqrt(zPrimeX * zPrimeX + zPrimeY * zPrimeY);

        if (std::abs(norm) > 2){
            return static_cast<double>(currIters);
        }

        zX = zPrimeX;
        zY = zPrimeY;
        currIters++;
    }

    return std::numeric_limits<double>::infinity();  // get rid of this and add your code here...
}

double MandelbrotViewer::mandelbrotSmooth(double cX, double cY, int maxIters) const {
    // TODO: return the smoothed number of iterations it takes for z to escape a radius of greater
    //       than 2, if it happens within maxIters iterations, otherwise return infinity.
    //       If you use an escape radius of exactly 2, you will see some artifacts. Use a
    //       higher radius (this is still correct, since divergence -> infty), but with more
    //       computational cost (since you need to simulate more steps).
    int currIters = 0;

    double zX = 0.0;
    double zY = 0.0;

    double zPrimeX = 0.0;
    double zPrimeY = 0.0;

    while (currIters < maxIters) {
        zPrimeX = zX * zX - zY * zY + cX;
        zPrimeY = 2.0 * zX * zY + cY;

        float norm = std::sqrt(zPrimeX * zPrimeX + zPrimeY * zPrimeY);

        if (std::abs(norm) > 4){
            return static_cast<double>((currIters + 1) - ((std::log(std::log(norm)) / LOG_2)));
        }

        zX = zPrimeX;
        zY = zPrimeY;
        currIters++;
    }

    return std::numeric_limits<double>::infinity();
}

// windowPosToWorld takes a point in window coordinates and converts it to world coordinates
sf::Vector2<double> MandelbrotViewer::windowPosToWorld(const sf::Vector2<double>& pWindow) {
    // TODO: given a point in window coordinates (by default SFML gives these as sf::Vector2i,
    //       the caller will have to cast to sf::Vector2<double>), convert them into world
    //       coordinates in the context of the current world view.


    double coordX = mMinPointWorld.x + ((pWindow.x + 0.5) / mWindowSize.x) * (mMaxPointWorld.x - mMinPointWorld.x);
    double coordY = mMinPointWorld.y + ((pWindow.y + 0.5) / mWindowSize.y) * (mMaxPointWorld.y - mMinPointWorld.y);


    return {static_cast<double>(coordX), static_cast<double>(coordY)};
}

// drawIntoBuffer renders the current world view (bounded by mMinPointWorld and mMaxPointWorld)
// into mViewBuffer
void MandelbrotViewer::drawIntoViewBuffer(int maxIters) {
    // TODO: render into mViewBuffer using sf::Image's setPixel method, which has signature
    //          void sf::Image::setPixel(sf::Vector2u coords, sf::Color color)
    //       At each pixel, find the world coordinates corresponding to the **CENTER** of the pixel.
    //       Then, find the (possibly continuous) number of iterations it takes for z to escape
    //       the escape radius (using mandelbrotSmooth() or mandelbrot()). If it never escapes,
    //       color the pixel black, otherwise, pass the escape iteration number to
    //       CyclicGradient::DEFAULT_GRADIENT(n) to get a colour to set the pixel to.
    
    for (int x = 0; x < mWindowSize.x - 1; x++) {
        for (int y = 0; y < mWindowSize.y; y++) {
            sf::Vector2<double> currPixel({static_cast<double>(x), static_cast<double>(y)});
            sf::Vector2<double> worldPos = MandelbrotViewer::windowPosToWorld(currPixel);
            double escapeTime = MandelbrotViewer::mandelbrot(worldPos.x, worldPos.y, maxIters);

            sf::Color color;

            if (escapeTime == std::numeric_limits<double>::infinity()) {
                color = sf::Color::Black;
            } else {
                color = CyclicGradient::DEFAULT_GRADIENT(escapeTime);
            }

            mViewBuffer.setPixel({static_cast<unsigned int>(currPixel.x), static_cast<unsigned int>(currPixel.y)}, color);

        }


    }
}

// copyViewBufferToGPU takes the drawn CPU-side buffer mViewBuffer and copies it to the
// GPU-side.
void MandelbrotViewer::copyViewBufferToGPU() {
    // TODO: load mViewBuffer from the CPU into mViewBufferGPU on the GPU.
    // Hint: this is a one-liner.
    mViewBufferGPU.loadFromImage(mViewBuffer);

}

// draw clears the window, draws the view, as well as the text with its shadow underneath
// it. Finally, the window is displayed.
void MandelbrotViewer::draw() {
    mWindow.clear();
    mWindow.draw(mViewSprite);
    mWindow.draw(mCursorWorldPosTextShadow);
    mWindow.draw(mCursorWorldPosText);
    mWindow.display();
}
