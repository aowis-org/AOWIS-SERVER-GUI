#pragma once

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>

// One binding per selected property. Accessors must resolve entity identity on
// each call; an unavailable/deleted property returns nullopt. The writer must
// use the application's mutation/undo command and report acceptance.
// Conversion callbacks use the central Model converter for the field quantity.
// No Qt dependency, subscription, control rounding, or direct model setter.
class NumericPropertyBinding
{
public:
    enum class State { Clean, Editing, Dirty, Committing };
    enum class Result { Unchanged, Committed, Invalid, Unavailable, Conflict, Rejected, Busy };
    using Reader = std::function<std::optional<double>()>;
    using Writer = std::function<bool(double)>;
    using Converter = std::function<std::optional<double>(double, const std::string &)>;
    using Validator = std::function<bool(double)>;

    NumericPropertyBinding(Reader read, Writer write, Converter from_canonical,
                           Converter to_canonical, Validator validate = {})
        : read_(std::move(read)), write_(std::move(write)),
          from_canonical_(std::move(from_canonical)), to_canonical_(std::move(to_canonical)),
          validate_(std::move(validate)) {}

    [[nodiscard]] State state() const { return this->state_; }
    [[nodiscard]] const std::string &editUnit() const { return this->edit_unit_; }
    [[nodiscard]] std::optional<double> pendingCanonical() const { return this->pending_; }

    // Read-only even while an edit is pending. Control adapters block signals
    // around all refreshes and never call userEdited for programmatic changes.
    [[nodiscard]] std::optional<double> presentation(const std::string &unit) const {
        const std::optional<double> value = this->read_();
        if (!value || !std::isfinite(*value)) return std::nullopt;
        return this->from_canonical_(*value, unit);
    }

    // represented_value is the control's actual rounded/quantized baseline.
    // Call before the first user change, not after the widget changes value.
    bool beginEdit(const std::string &unit, double represented_value) {
        if (this->state_ != State::Clean || !std::isfinite(represented_value)) return false;
        const std::optional<double> value = this->read_();
        if (!value || !std::isfinite(*value) || !this->from_canonical_(*value, unit)) return false;
        this->original_ = *value;
        this->baseline_ = represented_value;
        this->edit_unit_ = unit;
        this->state_ = State::Editing;
        return true;
    }

    // Only an adapter-confirmed user edit may call this. Invalid/incomplete text
    // calls invalidInput instead. The original edit unit stays pinned across
    // profile changes; pending input is never reinterpreted in a new unit.
    bool userEdited(double represented_value) {
        if (this->state_ != State::Editing && this->state_ != State::Dirty) return false;
        this->pending_.reset();
        this->state_ = State::Dirty;
        if (!std::isfinite(represented_value)) return false;
        if (represented_value == this->baseline_) {
            this->state_ = State::Editing;
            return true;
        }
        this->pending_ = this->to_canonical_(represented_value, this->edit_unit_);
        if (!this->pending_ || !std::isfinite(*this->pending_) ||
            (this->validate_ && !this->validate_(*this->pending_))) {
            this->pending_.reset();
            return false;
        }
        return true;
    }

    void invalidInput() {
        if (this->state_ == State::Editing || this->state_ == State::Dirty) {
            this->pending_.reset();
            this->state_ = State::Dirty;
        }
    }

    Result commit() {
        if (this->state_ == State::Committing) return Result::Busy;
        if (this->state_ != State::Dirty) {
            cancel();
            return Result::Unchanged;
        }
        if (!this->pending_) return Result::Invalid;
        const std::optional<double> current = this->read_();
        if (!current) return Result::Unavailable;
        if (*current != this->original_) return Result::Conflict;
        if (*this->pending_ == this->original_) {
            cancel();
            return Result::Unchanged;
        }
        this->state_ = State::Committing;
        bool accepted;
        try { accepted = this->write_(*this->pending_); }
        catch (...) { this->state_ = State::Dirty; throw; }
        this->state_ = State::Dirty;
        if (!accepted) return Result::Rejected;
        cancel();
        return Result::Committed;
    }

    void cancel() {
        if (this->state_ == State::Committing) return;
        this->pending_.reset();
        this->edit_unit_.clear();
        this->state_ = State::Clean;
    }

private:
    Reader read_;
    Writer write_;
    Converter from_canonical_;
    Converter to_canonical_;
    Validator validate_;
    State state_ = State::Clean;
    double original_ = 0.0;
    double baseline_ = 0.0;
    std::string edit_unit_;
    std::optional<double> pending_;
};
