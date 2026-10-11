#pragma once

#include "numeric_property_binding.h"
#include "numeric_text.h"
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QEvent>
#include <QString>
#include <algorithm>
#include <limits>
#include <type_traits>

// Bindings must outlive their controls. One binding per control; compound
// editors refresh their peers after an accepted mutation. Views call refresh
// from UnitAwareView, so controls do not subscribe individually to profiles.
struct NumericEditorPresentation
{
    std::string unit;
    QString suffix;
    double minimum = 0.0;
    double maximum = 100.0;
    double step = 1.0;
    int decimals = 2;
    bool grouping = false;
    std::optional<QLocale> locale;
};

template<class SpinBox>
class NumericSpinBox : public SpinBox
{
public:
    explicit NumericSpinBox(NumericPropertyBinding &binding, QWidget *parent = nullptr)
        : SpinBox(parent), binding_(binding)
    {
        SpinBox::setKeyboardTracking(false);
        QObject::connect(SpinBox::lineEdit(), &QLineEdit::textEdited, this,
            [this](const QString &text) {
                if (!begin()) return;
                // Qt may normalize a spin box's line edit on focus/visibility
                // transitions. Retain the actual user input independently.
                this->pending_text_ = text;
                QString candidate = text;
                int position = candidate.size();
                if (SpinBox::validate(candidate, position) != QValidator::Acceptable) {
                    this->binding_.invalidInput();
                    return;
                }
                this->binding_.userEdited(static_cast<double>(SpinBox::valueFromText(text)));
            });
        QObject::connect(this, &QAbstractSpinBox::editingFinished, this, [this] {
            // Qt can emit editingFinished while hiding an editor. Visibility
            // changes are presentation operations, not user commits; the
            // explicit focus-out/Enter paths below resolve genuine edits.
            if (this->visibility_event_depth_ != 0 || !SpinBox::isVisible()) return;
            commitEdit();
        });
    }

    // Bounds/step are already expressed in the requested display unit.
    bool refresh(const NumericEditorPresentation &presentation) {
        if (!std::isfinite(presentation.minimum) || !std::isfinite(presentation.maximum) ||
            !std::isfinite(presentation.step) || presentation.minimum > presentation.maximum ||
            presentation.step <= 0.0 || presentation.decimals < 0 || presentation.decimals > 323)
            return false;
        if constexpr (std::is_same_v<SpinBox, QSpinBox>) {
            if (presentation.minimum < std::numeric_limits<int>::min() ||
                presentation.maximum > std::numeric_limits<int>::max() ||
                presentation.step > std::numeric_limits<int>::max() ||
                std::trunc(presentation.minimum) != presentation.minimum ||
                std::trunc(presentation.maximum) != presentation.maximum ||
                std::trunc(presentation.step) != presentation.step) return false;
        }
        this->latest_ = presentation;
        if (this->binding_.state() != NumericPropertyBinding::State::Clean) return true;
        std::optional<double> value = this->binding_.presentation(presentation.unit);
        if (value && !std::isfinite(*value)) value.reset();
        const QSignalBlocker blocker(this);
        const QSignalBlocker text_blocker(SpinBox::lineEdit());
        SpinBox::setEnabled(value.has_value());
        if (!value) return false;
        if (presentation.locale) SpinBox::setLocale(*presentation.locale);
        if constexpr (std::is_same_v<SpinBox, QDoubleSpinBox>) {
            SpinBox::setDecimals(presentation.decimals);
            SpinBox::setRange(presentation.minimum, presentation.maximum);
            SpinBox::setSingleStep(presentation.step);
            SpinBox::setValue(*value);
        } else {
            SpinBox::setRange(static_cast<int>(presentation.minimum), static_cast<int>(presentation.maximum));
            SpinBox::setSingleStep(static_cast<int>(presentation.step));
            SpinBox::setValue(static_cast<int>(std::round(std::clamp(*value, presentation.minimum, presentation.maximum))));
        }
        SpinBox::setGroupSeparatorShown(presentation.grouping);
        SpinBox::setSuffix(presentation.suffix);
        this->baseline_ = SpinBox::value();
        this->display_unit_ = presentation.unit;
        return true;
    }

    NumericPropertyBinding::Result commitEdit() {
        this->last_result_ = this->binding_.commit();
        // A read-only calculated field may show a value different from the
        // stored property. Focus loss/Enter/Escape must not replace that
        // calculated presentation with the stored property value.
        if (this->binding_.state() == NumericPropertyBinding::State::Clean) {
            this->pending_text_.reset();
            if (!SpinBox::isReadOnly()) refresh(this->latest_);
        }
        return this->last_result_;
    }
    void cancelEdit() {
        this->binding_.cancel();
        this->pending_text_.reset();
        if (!SpinBox::isReadOnly()) refresh(this->latest_);
    }
    NumericPropertyBinding::Result lastResult() const { return this->last_result_; }
    const std::string &displayUnit() const { return this->display_unit_; }

protected:
    bool event(QEvent *event) override {
        const bool visibility = event->type() == QEvent::Hide ||
            event->type() == QEvent::Show;
        const bool restore = visibility || event->type() == QEvent::FocusOut;
        // Qt's internal spin-box normalization may run while delivering a
        // visibility event. Capture the original draft before that happens;
        // never let an internal edit notification replace the user's text.
        const std::optional<QString> draft = visibility ? this->pending_text_ : std::nullopt;
        if (visibility) ++this->visibility_event_depth_;
        const bool handled = SpinBox::event(event);
        if (visibility) {
            --this->visibility_event_depth_;
            if (draft && this->binding_.state() != NumericPropertyBinding::State::Clean)
                this->pending_text_ = *draft;
        }
        if (restore) restorePendingText();
        return handled;
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (resolveKey(event)) return;
        SpinBox::keyPressEvent(event);
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == SpinBox::lineEdit()) {
            if (event->type() == QEvent::KeyPress &&
                resolveKey(static_cast<QKeyEvent *>(event))) return true;
            if (event->type() == QEvent::FocusOut ||
                event->type() == QEvent::Hide || event->type() == QEvent::Show) {
                const std::optional<QString> draft = this->pending_text_;
                const bool handled = SpinBox::eventFilter(object, event);
                if (draft && this->binding_.state() != NumericPropertyBinding::State::Clean)
                    this->pending_text_ = *draft;
                restorePendingText();
                return handled;
            }
        }
        return SpinBox::eventFilter(object, event);
    }
    void focusOutEvent(QFocusEvent *event) override {
        // Qt normally fixes incomplete text before emitting editingFinished.
        // Preserve the actual pending input, then resolve our transaction.
        const QString pending_text = this->pending_text_.value_or(SpinBox::lineEdit()->text());
        {
            const QSignalBlocker blocker(this);
            const QSignalBlocker text_blocker(SpinBox::lineEdit());
            SpinBox::focusOutEvent(event);
            if (this->binding_.state() != NumericPropertyBinding::State::Clean)
                SpinBox::lineEdit()->setText(pending_text);
        }
        commitEdit();
    }

    void stepBy(int steps) override {
        if (SpinBox::isReadOnly()) return;
        // An arrow must not silently replace incomplete input with Qt's fixup.
        if (this->binding_.state() == NumericPropertyBinding::State::Dirty &&
            !this->binding_.pendingCanonical()) return;
        if (!begin()) return;
        SpinBox::stepBy(steps);
        this->binding_.userEdited(SpinBox::value());
        commitEdit();
    }

private:
    void restorePendingText() {
        if (!this->pending_text_ ||
            this->binding_.state() == NumericPropertyBinding::State::Clean ||
            SpinBox::lineEdit()->text() == *this->pending_text_) return;
        const QSignalBlocker blocker(this);
        const QSignalBlocker text_blocker(SpinBox::lineEdit());
        SpinBox::lineEdit()->setText(*this->pending_text_);
    }
    bool resolveKey(QKeyEvent *event) {
        if (event->key() == Qt::Key_Escape) cancelEdit();
        else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) commitEdit();
        else return false;
        event->accept();
        return true;
    }
    bool begin() {
        if (this->binding_.state() == NumericPropertyBinding::State::Clean)
            return this->binding_.beginEdit(this->display_unit_, this->baseline_);
        return this->binding_.state() != NumericPropertyBinding::State::Committing;
    }
    NumericPropertyBinding &binding_;
    std::optional<QString> pending_text_;
    int visibility_event_depth_ = 0;
    NumericEditorPresentation latest_;
    std::string display_unit_;
    double baseline_ = 0.0;
    NumericPropertyBinding::Result last_result_ = NumericPropertyBinding::Result::Unchanged;
};

using NumericDoubleSpinBox = NumericSpinBox<QDoubleSpinBox>;
using NumericIntegerSpinBox = NumericSpinBox<QSpinBox>;

class NumericSlider : public QSlider
{
public:
    explicit NumericSlider(NumericPropertyBinding &binding, QWidget *parent = nullptr)
        : QSlider(Qt::Horizontal, parent), binding_(binding)
    {
        QObject::connect(this, &QAbstractSlider::sliderReleased, this, [this] {
            refresh(this->latest_, this->latest_positions_);
        });
        QObject::connect(this, &QAbstractSlider::sliderMoved, this, [this](int position) { edit(position); });
        QObject::connect(this, &QAbstractSlider::actionTriggered, this, [this](int) { edit(sliderPosition()); });
    }
    bool refresh(const NumericEditorPresentation &presentation, int positions = 1000) {
        if (positions <= 0 || !std::isfinite(presentation.minimum) ||
            !std::isfinite(presentation.maximum) || presentation.maximum <= presentation.minimum ||
            !std::isfinite(presentation.maximum - presentation.minimum)) return false;
        this->latest_ = presentation;
        this->latest_positions_ = positions;
        // Freeze the mapping for the entire drag, including profile changes.
        if (isSliderDown()) return true;
        this->presentation_ = presentation;
        this->positions_ = positions;
        std::optional<double> value = this->binding_.presentation(presentation.unit);
        if (value && !std::isfinite(*value)) value.reset();
        const QSignalBlocker blocker(this);
        setEnabled(value.has_value());
        if (!value) return false;
        setRange(0, positions);
        const double fraction = (std::clamp(*value, presentation.minimum, presentation.maximum) - presentation.minimum)
            / (presentation.maximum - presentation.minimum);
        setValue(static_cast<int>(std::round(fraction * positions)));
        this->baseline_ = represented(QSlider::value());
        return true;
    }
    NumericPropertyBinding::Result lastResult() const { return this->last_result_; }

private:
    double represented(int position) const {
        const double fraction = static_cast<double>(position) / this->positions_;
        return this->presentation_.minimum + fraction * (this->presentation_.maximum - this->presentation_.minimum);
    }
    void edit(int position) {
        if (!this->binding_.beginEdit(this->presentation_.unit, this->baseline_)) return;
        this->binding_.userEdited(represented(position));
        this->last_result_ = this->binding_.commit();
        // Discrete slider edits have no incomplete text to preserve. Failures
        // restore authoritative presentation and remain observable via result.
        this->binding_.cancel();
        if (isSliderDown()) {
            const QSignalBlocker blocker(this);
            setSliderPosition(position);
            this->baseline_ = represented(position);
        } else {
            refresh(this->latest_, this->latest_positions_);
        }
    }
    NumericPropertyBinding &binding_;
    NumericEditorPresentation presentation_;
    NumericEditorPresentation latest_;
    int latest_positions_ = 1000;
    int positions_ = 1000;
    double baseline_ = 0.0;
    NumericPropertyBinding::Result last_result_ = NumericPropertyBinding::Result::Unchanged;
};

// Plain numeric text; unit labels are supplied by the containing view.
class NumericLineEdit : public QLineEdit
{
public:
    explicit NumericLineEdit(NumericPropertyBinding &binding, QWidget *parent = nullptr)
        : QLineEdit(parent), binding_(binding)
    {
        QObject::connect(this, &QLineEdit::textEdited, this, [this](const QString &text) {
            if (this->binding_.state() == NumericPropertyBinding::State::Clean &&
                !this->binding_.beginEdit(this->display_unit_, this->baseline_)) return;
            const NumericText::ParseResult parsed = NumericText::parse(text, this->edit_locale_, this->edit_policy_.grouping);
            if (parsed.value) this->binding_.userEdited(*parsed.value);
            else this->binding_.invalidInput();
        });
        QObject::connect(this, &QLineEdit::editingFinished, this, [this] { commitEdit(); });
    }
    bool refresh(const std::string &unit, int significant_digits = 12) {
        NumericFormatPolicy policy;
        policy.precision = significant_digits;
        return refresh(unit, policy);
    }
    bool refresh(const std::string &unit, const NumericFormatPolicy &policy) {
        if (!policy.valid()) return false;
        this->latest_unit_ = unit;
        this->latest_policy_ = policy;
        if (this->binding_.state() != NumericPropertyBinding::State::Clean) return true;
        std::optional<double> value = this->binding_.presentation(unit);
        if (value && !std::isfinite(*value)) value.reset();
        const QSignalBlocker blocker(this);
        setEnabled(value.has_value());
        if (!value) return false;
        this->edit_locale_ = locale();
        this->edit_policy_ = policy;
        setText(*NumericText::format(*value, this->edit_locale_, policy));
        const NumericText::ParseResult parsed = NumericText::parse(text(), this->edit_locale_, policy.grouping);
        if (!parsed.value) { setEnabled(false); return false; }
        this->baseline_ = *parsed.value;
        this->display_unit_ = unit;
        return true;
    }
    NumericPropertyBinding::Result commitEdit() {
        const NumericPropertyBinding::Result result = this->binding_.commit();
        if (this->binding_.state() == NumericPropertyBinding::State::Clean) refresh(this->latest_unit_, this->latest_policy_);
        return result;
    }
    void cancelEdit() { this->binding_.cancel(); refresh(this->latest_unit_, this->latest_policy_); }
    const std::string &displayUnit() const { return this->display_unit_; }
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) cancelEdit();
        else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) commitEdit();
        else { QLineEdit::keyPressEvent(event); return; }
        event->accept();
    }
private:
    NumericPropertyBinding &binding_;
    std::string display_unit_;
    std::string latest_unit_;
    QLocale edit_locale_;
    double baseline_ = 0.0;
    NumericFormatPolicy latest_policy_;
    NumericFormatPolicy edit_policy_;
};
