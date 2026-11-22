// Copyright 2026 Dao Browser Authors. All rights reserved.
#ifndef DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_NOTIFICATION_H_
#define DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_NOTIFICATION_H_

#include <memory>

#include "chrome/browser/notifications/notification_handler.h"

namespace dao {
inline constexpr auto kDaoResourceUsageNotificationType =
    NotificationHandler::Type::DAO_RESOURCE_USAGE;

std::unique_ptr<NotificationHandler> CreateResourceUsageNotificationHandler();
}  // namespace dao

#endif  // DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_NOTIFICATION_H_
