#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace SilverColours
{
    const juce::Colour silverLight { 0xffeef0f3 };
    const juce::Colour silverMid   { 0xffcfd4da };
    const juce::Colour silverDark  { 0xff9aa2ac };
    const juce::Colour navy        { 0xff14284b };
    const juce::Colour navySoft    { 0xff3a5078 };
    const juce::Colour screenTop   { 0xff0f1c3d };
    const juce::Colour screenBot   { 0xff070f26 };
    const juce::Colour gridLine    { 0xff2a3d68 };
    const juce::Colour gridText    { 0xff8ea4cc };
    const juce::Colour teal        { 0xff38c7c0 };
    const juce::Colour yellow      { 0xffffcf4a };
    const juce::Colour orange      { 0xffff9a2e };
    const juce::Colour red         { 0xffff4747 };
    const juce::Colour midBlue     { 0xff86bcff };
    const juce::Colour sideOrange  { 0xffffa347 };
    const juce::Colour buttonOn    { 0xff24457d };
    const juce::Colour field       { 0xfff6f7f9 };

    // cor da barra conforme a correlacao
    inline juce::Colour forCorrelation (float c)
    {
        if (c >= 0.5f) return teal;
        if (c >= 0.0f) return yellow.interpolatedWith (teal, c / 0.5f);
        return orange.interpolatedWith (red, juce::jlimit (0.0f, 1.0f, -c * 2.0f));
    }
}

inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

class SilverLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SilverLookAndFeel()
    {
        using namespace SilverColours;
        setColour (juce::ResizableWindow::backgroundColourId, silverMid);
        setColour (juce::Label::textColourId, navy);

        setColour (juce::TextButton::buttonColourId,   silverMid);
        setColour (juce::TextButton::buttonOnColourId, buttonOn);
        setColour (juce::TextButton::textColourOffId,  navy);
        setColour (juce::TextButton::textColourOnId,   juce::Colours::white);

        setColour (juce::ComboBox::backgroundColourId, field);
        setColour (juce::ComboBox::textColourId,       navy);
        setColour (juce::ComboBox::outlineColourId,    silverDark);
        setColour (juce::ComboBox::arrowColourId,      navy);

        setColour (juce::PopupMenu::backgroundColourId,            field);
        setColour (juce::PopupMenu::textColourId,                  navy);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, buttonOn);
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

        setColour (juce::Slider::trackColourId,             navySoft);
        setColour (juce::Slider::backgroundColourId,        silverDark.withAlpha (0.6f));
        setColour (juce::Slider::thumbColourId,             navy);
        setColour (juce::Slider::textBoxTextColourId,       navy);
        setColour (juce::Slider::textBoxBackgroundColourId, field);
        setColour (juce::Slider::textBoxOutlineColourId,    silverDark);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();

        juce::Colour top = on ? SilverColours::buttonOn.brighter (0.25f) : juce::Colour (0xfffbfcfd);
        juce::Colour bot = on ? SilverColours::buttonOn.darker (0.2f)    : backgroundColour.darker (0.08f);
        if (isHighlighted) { top = top.brighter (0.05f); bot = bot.brighter (0.05f); }
        if (isDown)        std::swap (top, bot);

        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (SilverColours::silverDark);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace SilverColours;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float alpha = slider.isEnabled() ? 1.0f : 0.45f;

        // trilho e arco do valor
        juce::Path track, value;
        const float arcR = radius - 2.0f;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (silverDark.withAlpha (0.55f * alpha));
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (navySoft.withAlpha (alpha));
        g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // corpo prata
        const float kr = radius - 8.0f;
        auto knob = juce::Rectangle<float> (kr * 2.0f, kr * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillEllipse (knob.translated (0.0f, 2.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffdfdfe), knob.getX(), knob.getY(),
                                                 silverDark, knob.getRight(), knob.getBottom(), false));
        g.fillEllipse (knob);
        g.setColour (silverDark.darker (0.2f));
        g.drawEllipse (knob, 1.0f);

        // ponteiro
        juce::Path ptr;
        ptr.addRoundedRectangle (-1.5f, -kr + 3.0f, 3.0f, kr * 0.55f, 1.5f);
        g.setColour (navy.withAlpha (alpha));
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.5f), juce::Font::bold));
    }
};
