#ifndef CLASS_HIT_AI_ADVICE_H
#define CLASS_HIT_AI_ADVICE_H

#include <QString>

struct ClassHitAdviceContext {
    QString studentName;
    int hitIdx = 0;
    QString hitType;
    int score = 0;
    int speedKmh = -1;
    int powerTen = -1;
    int durationMs = -1;
};

QString pickClassHitAiAdvice(const ClassHitAdviceContext &ctx);

#endif
