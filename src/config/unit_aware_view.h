#pragma once

#include "config/unit_presentation_context.h"

#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <utility>

// A presentation boundary, not an individual numeric editor. Construct with the
// view whose visibility determines whether its values need refreshing.
// The callback must only update presentation; it must never write model values.
class UnitAwareView final : public QObject
{
public:
    using Refresh = std::function<void()>;

    UnitAwareView(QWidget *view, UnitPresentationContext &context, Refresh refresh)
        : QObject(view), view_(view), context_(context), refresh_(std::move(refresh))
    {
        Q_ASSERT(view);
        view->installEventFilter(this);
        this->context_.subscribe(this, [this](UnitPresentationContext::Generation) {
            this->refreshIfNeeded();
        });
        // If already visible, initialize immediately. Otherwise the first
        // Show event (or an explicit refreshIfNeeded) will do it.
        this->refreshIfNeeded();
    }

    [[nodiscard]] UnitPresentationContext::Generation appliedGeneration() const
    {
        return this->applied_generation_;
    }

    // Also call after replacing the entity/content displayed by this view.
    // forceRefresh() is separate because a content change need not change units.
    void forceRefresh()
    {
        this->applied_generation_ = 0;
        this->refreshIfNeeded();
    }

    void refreshIfNeeded()
    {
        if (!this->view_ || !this->view_->isVisible() || this->refreshing_ || !this->refresh_)
            return;
        const UnitPresentationContext::Generation target = this->context_.generation();
        if (this->applied_generation_ == target)
            return;
        this->refreshing_ = true;
        this->refresh_();
        this->applied_generation_ = target;
        this->refreshing_ = false;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == this->view_ && event->type() == QEvent::Show) {
            // QWidget has entered its visible lifecycle. A parent may still be
            // hidden, in which case its eventual Show causes another event.
            this->refreshIfNeeded();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QWidget> view_;
    UnitPresentationContext &context_;
    Refresh refresh_;
    UnitPresentationContext::Generation applied_generation_ = 0;
    bool refreshing_ = false;
};
