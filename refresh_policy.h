#ifndef REFRESH_POLICY_H
#define REFRESH_POLICY_H

#include <stdbool.h>

#define AUTO_REFRESH_SECONDS 300.0
#define FIRST_MANUAL_REFRESH_SECONDS 5.0
#define MANUAL_REFRESH_SECONDS 30.0
#define INITIAL_RETRY_SECONDS 60.0
#define MAX_RETRY_SECONDS 900.0

typedef struct {
    double nextRefresh;
    double manualAllowedAt;
    double retryDelay;
    bool hasSuccessfulUpdate;
} RefreshPolicy;

static inline RefreshPolicy RefreshPolicyInit(double now)
{
    RefreshPolicy policy = {now, now, INITIAL_RETRY_SECONDS, false};
    return policy;
}

static inline bool RefreshPolicyDue(const RefreshPolicy *policy, double now,
                                   bool manualRequested, bool fetching)
{
    if (fetching) return false;
    return now >= policy->nextRefresh ||
           (manualRequested && now >= policy->manualAllowedAt);
}

static inline void RefreshPolicyStarted(RefreshPolicy *policy, double now)
{
    policy->manualAllowedAt = now + (policy->hasSuccessfulUpdate
        ? MANUAL_REFRESH_SECONDS : FIRST_MANUAL_REFRESH_SECONDS);
    policy->nextRefresh = now + AUTO_REFRESH_SECONDS;
}

static inline void RefreshPolicyFinished(RefreshPolicy *policy, double now,
                                        bool success)
{
    if (success) {
        policy->nextRefresh = now + AUTO_REFRESH_SECONDS;
        policy->manualAllowedAt = now + (policy->hasSuccessfulUpdate
            ? MANUAL_REFRESH_SECONDS : FIRST_MANUAL_REFRESH_SECONDS);
        policy->hasSuccessfulUpdate = true;
        policy->retryDelay = INITIAL_RETRY_SECONDS;
    } else {
        policy->nextRefresh = now + policy->retryDelay;
        /* Manual input must not bypass the retry delay. */
        policy->manualAllowedAt = policy->nextRefresh;
        policy->retryDelay *= 2.0;
        if (policy->retryDelay > MAX_RETRY_SECONDS) {
            policy->retryDelay = MAX_RETRY_SECONDS;
        }
    }
}

static inline int RefreshSecondsLeft(double deadline, double now)
{
    double remaining = deadline - now;
    if (remaining <= 0.0) return 0;
    int whole = (int)remaining;
    return whole + (remaining > (double)whole);
}

#endif
