#include "class_hit_ai_advice.h"

#include <QHash>
#include <QStringList>
#include <QtGlobal>

namespace {

enum StrokeKind {
    StrokeClear = 0,
    StrokeDrive,
    StrokeLift,
    StrokeNet,
    StrokeServe,
    StrokeSmash,
    StrokeGeneric,
    StrokeCount
};

enum ScoreTier {
    TierExcellent = 0, /* 85+ */
    TierGood,          /* 75-84 */
    TierAverage,       /* 65-74 */
    TierBasic,         /* 50-64 */
    TierCount
};

enum PowerFit {
    PowerLow = 0,
    PowerIdeal,
    PowerHigh,
    PowerUnknown,
    PowerFitCount
};

struct StrokePowerRange {
    int ideal;
    int minOk;
    int maxOk;
};

static const StrokePowerRange kStrokePower[StrokeCount] = {
    {7, 5, 9},  /* 高远 */
    {6, 4, 8},  /* 平抽 */
    {5, 3, 7},  /* 挑球 */
    {3, 1, 5},  /* 放网 */
    {5, 3, 7},  /* 发球 */
    {9, 7, 10}, /* 杀球 */
    {5, 2, 8},  /* 挥拍 */
};

static StrokeKind strokeFromLabel(const QString &hitType)
{
    const QString t = hitType.trimmed();
    if (t.contains(QStringLiteral("杀")))
        return StrokeSmash;
    if (t.contains(QStringLiteral("高远")))
        return StrokeClear;
    if (t.contains(QStringLiteral("平抽")))
        return StrokeDrive;
    if (t.contains(QStringLiteral("挑")))
        return StrokeLift;
    if (t.contains(QStringLiteral("放网")) || t.contains(QStringLiteral("搓")))
        return StrokeNet;
    if (t.contains(QStringLiteral("发")))
        return StrokeServe;
    return StrokeGeneric;
}

static ScoreTier scoreTier(int score)
{
    if (score >= 85)
        return TierExcellent;
    if (score >= 75)
        return TierGood;
    if (score >= 65)
        return TierAverage;
    return TierBasic;
}

static PowerFit powerFitFor(StrokeKind stroke, int powerTen)
{
    if (powerTen < 1)
        return PowerUnknown;
    const StrokePowerRange &r = kStrokePower[stroke];
    if (powerTen < r.minOk)
        return PowerLow;
    if (powerTen > r.maxOk)
        return PowerHigh;
    return PowerIdeal;
}

static int pickIndex(int poolSize, quint32 seed)
{
    if (poolSize <= 0)
        return 0;
    return static_cast<int>(seed % static_cast<quint32>(poolSize));
}

static quint32 adviceSeed(const ClassHitAdviceContext &ctx, int salt)
{
    quint32 s = static_cast<quint32>(ctx.hitIdx * 131u + ctx.score * 17u + salt * 997u);
    s ^= static_cast<quint32>(qHash(ctx.studentName));
    s ^= static_cast<quint32>(qHash(ctx.hitType));
    if (ctx.powerTen >= 0)
        s ^= static_cast<quint32>(ctx.powerTen * 1009u);
    if (ctx.speedKmh >= 0)
        s ^= static_cast<quint32>(ctx.speedKmh * 1013u);
    return s;
}

static QString pickOne(const QStringList &pool, quint32 seed)
{
    if (pool.isEmpty())
        return QString();
    return pool.at(pickIndex(pool.size(), seed));
}

static QString pickOpener(quint32 seed)
{
    static const QStringList openers = {
        QStringLiteral("AI 教练："),
        QStringLiteral("智能分析："),
        QStringLiteral("训练助手："),
        QStringLiteral("根据 IMU + 动作识别模型，"),
        QStringLiteral("结合本次挥拍数据，"),
    };
    return pickOne(openers, seed);
}

/* ── 高远 ── */
static QStringList clearAdvice(ScoreTier tier)
{
    switch (tier) {
    case TierExcellent:
        return {
            QStringLiteral("高远球击球链完整，鞭打传递顺畅，落点深度潜力不错。下一拍可刻意练习「同一节奏、不同落点」的变化。"),
            QStringLiteral("本次高远节奏稳定，加速段与收拍衔接自然。建议保持现在的引拍高度，继续强化蹬转发力顺序。"),
            QStringLiteral("从波形看，你的高远已经具备「压后场」的框架。可以尝试在不变形的前提下略微加快拍头速度。"),
            QStringLiteral("整体动作成熟，击球点偏上且发力集中。若再加强核心转体，后场威胁还能再上一档。"),
            QStringLiteral("这是一次质量很高的高远练习，挥拍时长与力度匹配良好。建议固定这套节奏，再练 10 拍连贯后场。"),
        };
    case TierGood:
        return {
            QStringLiteral("高远基本到位，但加速段还可以更「脆」一些。注意蹬地→转体→甩腕的顺序，不要提前收拍。"),
            QStringLiteral("击球框架正确，落点深度尚可提升。建议下一组专注「拍面稍关、发力再送一点」。"),
            QStringLiteral("整体像样，引拍高度略低时容易打不远。试着把非持拍手再抬高半拍，帮助打开胸廓。"),
            QStringLiteral("本次高远合格，收拍略早会损失球速。想象击球后拍头继续往目标方向「多送 20 厘米」。"),
            QStringLiteral("节奏基本稳定，若感觉球还差一点压底线，可在击球瞬间再压一下手腕加速。"),
        };
    case TierAverage:
        return {
            QStringLiteral("高远轮廓有了，但发力偏散，球速与深度还不够。先放慢节奏，把「转体带动大臂」练实。"),
            QStringLiteral("识别到完整挥拍，不过击球点可能偏后。建议侧身更充分，让击球点固定在右肩前上方。"),
            QStringLiteral("本次高远偏「推」而非「鞭打」。下一拍刻意放松小臂，用转体和甩腕去带速度。"),
            QStringLiteral("动作类型正确，力度略欠。可以先做无球挥拍，感受蹬地后力量传到拍面的路径。"),
            QStringLiteral("后场球需要更长的加速距离。引拍再充分一点，不要急于出手。"),
        };
    default:
        return {
            QStringLiteral("已识别为高远，但发力链条尚未完全打开。建议从分解练习开始：侧身、引拍、蹬转、击球四拍一组。"),
            QStringLiteral("本次高远还在建立肌肉记忆阶段。先保证击球点稳定，再逐步加大力量。"),
            QStringLiteral("动作方向对了，整体偏软。下一组用 70% 力量先把「打远、打高」的感觉找回来。"),
            QStringLiteral("高远需要完整挥拍，不要半挥。跟着口令「引—蹬—转—甩—收」再试几次。"),
            QStringLiteral("基础框架可继续打磨。建议对照标准高远，检查是否提前缩肘或未转体。"),
        };
    }
}

static QStringList driveAdvice(ScoreTier tier)
{
    switch (tier) {
    case TierExcellent:
        return {
            QStringLiteral("平抽节奏紧凑，拍面控制稳定，适合压制对手中场。可继续练习「快而不乱」的连贯平抽。"),
            QStringLiteral("本次平抽爆发点清晰，挥拍短促有力。保持拍头水平，下一拍可练反拍平抽对称性。"),
            QStringLiteral("中场平击质量很高，球速与弧度平衡不错。建议在多球练习里加入变线平抽。"),
            QStringLiteral("从 IMU 看，你的平抽「短促爆发」特征明显，这是好事。注意还原速度，准备下一拍。"),
            QStringLiteral("平抽到位，击球点前压充分。可尝试在保持速度的同时把落点压得更贴网。"),
        };
    case TierGood:
        return {
            QStringLiteral("平抽基本合格，再压一点拍头速度会更具威胁。注意引拍不要过大，保持紧凑。"),
            QStringLiteral("节奏对了，但偶尔偏「挑」的感觉。平抽要更平、更快，减少 upward 动作。"),
            QStringLiteral("本次平抽可用，还原略慢会影响连贯。击球后拍子迅速回到准备位。"),
            QStringLiteral("中场抽击框架正确，发力可以再集中在击球前 0.1 秒。"),
            QStringLiteral("整体不错，若球速还想提升，检查是否用了太多大臂而手腕加速不足。"),
        };
    case TierAverage:
        return {
            QStringLiteral("平抽动作识别成功，但爆发不够集中。缩短引拍，在击球瞬间「弹」出去。"),
            QStringLiteral("平抽需要拍面稳定、动作短。本次偏长，下一拍刻意减小挥拍幅度。"),
            QStringLiteral("中场球要快，不要等球掉太低。试着提前半步击球，拍头迎球。"),
            QStringLiteral("力度与节奏匹配度一般。平抽不是高远，不必用全场挥拍幅度。"),
            QStringLiteral("建议先做墙边小幅度快速挥拍，建立平抽的「短促鞭打」感觉。"),
        };
    default:
        return {
            QStringLiteral("已识别平抽意图，但动作偏松散。先练固定击球点的小幅度平击，再逐渐加力。"),
            QStringLiteral("平抽讲究「快、平、稳」。本次挥拍偏长，先缩小动作幅度。"),
            QStringLiteral("中场抽击还在适应中。下一组只追求拍面迎球，不追求大力。"),
            QStringLiteral("注意拍头高度与网平行，避免把平抽打成挑球或高远。"),
            QStringLiteral("基础阶段建议多球练习：教练喂球，你只练紧凑的一拍平抽。"),
        };
    }
}

static QStringList liftAdvice(ScoreTier tier)
{
    switch (tier) {
    case TierExcellent:
        return {
            QStringLiteral("挑球柔和到位，发力控制细腻，适合化解网前压力。可练习挑高与挑近的组合变化。"),
            QStringLiteral("本次挑球「轻、稳、准」，拍面打开角度合理。继续保持小幅度、高摩擦的击球方式。"),
            QStringLiteral("从数据看，你的挑球力度控制很好，没有过冲。下一步可练挑球后的快速回中。"),
            QStringLiteral("挑球质量高，球路有缓冲感。建议在被动情况下也保持这个放松的手感。"),
            QStringLiteral("动作识别与力度匹配——这是优秀挑球的标志。可尝试连续挑两拍不同深度。"),
        };
    case TierGood:
        return {
            QStringLiteral("挑球基本成功，再柔一点会更安全。击球瞬间放松手指，靠拍面「托」而不是「推」。"),
            QStringLiteral("本次挑球可用，落点还可更精准。注意拍面稍开，触球时间略长。"),
            QStringLiteral("整体像样，若球偶尔偏高，可能是发力略大。下一拍再收 10% 力量。"),
            QStringLiteral("挑球需要稳定手腕。本次略硬，想象用拍面「舀」起球。"),
            QStringLiteral("识别正确，建议多练低手位挑球，提升被动过渡能力。"),
        };
    case TierAverage:
        return {
            QStringLiteral("挑球意图对，但力度偏大，容易出界或过高。先以「过网即可」为目标。"),
            QStringLiteral("本次挑球偏「打」而非「搓挑」。缩小动作，增加拍面与球的接触感。"),
            QStringLiteral("被动挑球要稳，不要急于发力。下一拍专注柔和触球。"),
            QStringLiteral("挥拍幅度对挑球来说略大。参考放网的小动作，再向上送一点。"),
            QStringLiteral("建议练习网前多球：只挑不杀，培养轻触球的手感。"),
        };
    default:
        return {
            QStringLiteral("已识别挑球，但控制不足。先练静止球轻挑过网，再加入移动。"),
            QStringLiteral("挑球是「救球型」技术，力度宁小勿大。本次可再放松手腕。"),
            QStringLiteral("动作类型正确，还需建立细腻手感。多练拇指与食指的微调控制。"),
            QStringLiteral("挑球不要借鉴高远的大挥拍。下一组刻意缩短动作路径。"),
            QStringLiteral("基础阶段建议：教练手抛球，你只做柔和上挑，追求稳定过网。"),
        };
    }
}

static QStringList netAdvice(ScoreTier tier)
{
    switch (tier) {
    case TierExcellent:
        return {
            QStringLiteral("放网手感细腻，力度控制极佳，正是网前所需要的「轻触即走」。可练习假动作放网。"),
            QStringLiteral("本次放网几乎完美——短、贴、稳。保持这种放松的指尖控制，对手很难直接扑杀。"),
            QStringLiteral("从 IMU 看，你的放网爆发短而准，没有多余发力。建议练习放网后立刻举拍准备。"),
            QStringLiteral("网前小球质量很高，拍面角度与力度匹配。下一步可练「放网—勾对角」连贯。"),
            QStringLiteral("放网到位，球贴网而下。这种控制值得固定成你的网前「默认拍」。"),
        };
    case TierGood:
        return {
            QStringLiteral("放网基本成功，再轻 5% 会更贴网。击球时像「点一下」而非「送出去」。"),
            QStringLiteral("本次放网可用，还原速度还可以更快。小球之后立刻回到中心准备位。"),
            QStringLiteral("控制不错，若球略高，可能是拍面偏开或触球偏上。下一拍再压低一点。"),
            QStringLiteral("网前球识别准确，建议多练不同高度来球的放网，提升适应性。"),
            QStringLiteral("整体合格，放网后注意别站位太前，给对手挑后场留反应时间。"),
        };
    case TierAverage:
        return {
            QStringLiteral("放网意图对，但力度偏大，球容易偏高或离网。先以「刚过网」为唯一目标。"),
            QStringLiteral("本次放网偏「推」，需要更多「搓」的摩擦感。放松握拍，用手指微调。"),
            QStringLiteral("网前球动作要小，本次挥拍略长。下一拍只动小臂和手腕。"),
            QStringLiteral("识别为放网，力度与动作不匹配。放网不是高远，切忌用大挥拍。"),
            QStringLiteral("建议静态练习：站在网前，只练轻放，直到 10 球里 8 球贴网。"),
        };
    default:
        return {
            QStringLiteral("已识别放网，但控制还在建立中。先练无对抗轻放，培养「越轻越好」的意识。"),
            QStringLiteral("放网是精细技术，本次力度明显偏大。下一组只用 30% 力量。"),
            QStringLiteral("动作类型正确，需加强手指对拍面的微调。握拍放松是关键。"),
            QStringLiteral("网前小球不要抢节奏。等球下落到合适高度再轻触。"),
            QStringLiteral("基础建议：多练「拍面略关、向前下方轻送」的固定动作。"),
        };
    }
}

static QStringList serveAdvice(ScoreTier tier)
{
    switch (tier) {
    case TierExcellent:
        return {
            QStringLiteral("发球稳定，发力可控，适合作为每分的起点。可练习同一动作发近网与后场。"),
            QStringLiteral("本次发球节奏规范，没有多余晃动。建议固定这套预挥拍，再微调落点。"),
            QStringLiteral("从波形看，发球力度适中、动作可重复——这是高质量发球的标志。"),
            QStringLiteral("发球识别准确，击球点稳定。下一步可练发追身与发边角的变化。"),
            QStringLiteral("整体表现优秀，发球后举拍准备也很关键，记得快速进入下一拍。"),
        };
    case TierGood:
        return {
            QStringLiteral("发球基本合格，再强调「低起点、稳触球」。避免发球时身体起伏过大。"),
            QStringLiteral("本次发球可用，落点控制还可更细。练习同一高度击球，只改变拍面角度。"),
            QStringLiteral("节奏对了，若偶有不稳，检查非持拍手是否干扰平衡。"),
            QStringLiteral("发球不宜过大动作。本次略长，下一拍缩小引拍。"),
            QStringLiteral("识别正确，建议多练连续 20 个发球，建立肌肉记忆。"),
        };
    case TierAverage:
        return {
            QStringLiteral("发球框架有了，但稳定性不足。先不求变化，只练同一种发球 10 个一致。"),
            QStringLiteral("本次发球偏紧，放松肩肘会有帮助。发球是「控力」不是「全力」。"),
            QStringLiteral("动作类型对，击球点需固定。建议在镜子前检查发球时的站位。"),
            QStringLiteral("发球力度波动较大。下一组只用 60% 力量，追求重复性。"),
            QStringLiteral("建议分解练习：只做引拍到触球，不追求球速，先求过网。"),
        };
    default:
        return {
            QStringLiteral("已识别发球，基础还需巩固。从最短动作开始，保证每个球都能稳定过网。"),
            QStringLiteral("发球阶段建议减少变向，先固定一种正手或反手发球练熟。"),
            QStringLiteral("本次发球偏散，注意拍面与球的接触要干脆、单一。"),
            QStringLiteral("建立「同一站位、同一引拍、同一击球点」的三同一练习。"),
            QStringLiteral("发球是每一分的开始，先练稳定，再练刁钻。"),
        };
    }
}

static QStringList smashAdvice(ScoreTier tier)
{
    switch (tier) {
    case TierExcellent:
        return {
            QStringLiteral("杀球爆发集中，鞭打特征明显，具备压制对手的威胁。可练习点杀与重杀的节奏切换。"),
            QStringLiteral("本次杀球质量很高，加速段清晰、收拍完整。保持侧身引拍，继续强化蹬地爆发。"),
            QStringLiteral("从 IMU 看，你的杀球力度与时长匹配良好，这是有效杀球的信号。注意杀球后的连贯。"),
            QStringLiteral("杀球到位，拍头速度表现优秀。若再压低落点，对手更难接。"),
            QStringLiteral("整体非常像样，已具备「一拍制压」的雏形。建议练杀直线与杀斜线的选择。"),
        };
    case TierGood:
        return {
            QStringLiteral("杀球基本成功，再压一点拍头速度会更尖。注意击球点在身体右前上方。"),
            QStringLiteral("本次杀球可用，但偶有「推杀」。下一拍强调甩腕与内旋，不要只用手臂推。"),
            QStringLiteral("爆发不错，收拍略早会损失角度。击球后让拍头自然向下完整收尾。"),
            QStringLiteral("识别准确，若杀球不够尖，检查是否击球点偏后或引拍不足。"),
            QStringLiteral("整体合格，建议练「高点击球、快速下压」的杀球轨迹。"),
        };
    case TierAverage:
        return {
            QStringLiteral("杀球意图对，但爆发不够集中。先练无球鞭打，再加入击球。"),
            QStringLiteral("本次杀球偏「高远式」，下压不足。拍面稍关，击球瞬间再加速。"),
            QStringLiteral("力度与杀球要求有差距。杀球需要 80% 以上爆发，引拍要充分。"),
            QStringLiteral("动作类型正确，侧身与蹬地还需加强。杀球力量从脚起，不是从手起。"),
            QStringLiteral("建议先练抛球定点杀球，建立「高—快—尖」的杀球感觉。"),
        };
    default:
        return {
            QStringLiteral("已识别杀球，但发力链条未完全打开。分解练习：侧身、引拍、蹬转、下压。"),
            QStringLiteral("杀球还在建立阶段，不要急于重杀。先练动作完整，再 gradually 加力。"),
            QStringLiteral("本次偏软，杀球需要明确加速段。想象拍头「劈」向球。"),
            QStringLiteral("注意杀球不是越大越响，而是拍头速度。下一组专注鞭打感。"),
            QStringLiteral("基础建议：对墙练习下压挥拍，感受加速后再加入真实击球。"),
        };
    }
}

static QStringList genericAdvice(ScoreTier tier)
{
    switch (tier) {
    case TierExcellent:
        return {
            QStringLiteral("挥拍质量优秀，发力与节奏匹配良好。建议明确本拍意图后，再针对性强化某一技术。"),
            QStringLiteral("本次挥拍数据表现很好，动作连贯。可尝试在班级对比中保持这一节奏。"),
            QStringLiteral("从 IMU 波形看，击球爆发点清晰。继续固定引拍与击球点，稳定性会更高。"),
            QStringLiteral("整体完成度高，已具备良好挥拍框架。下一组可选定一种技术专项练习。"),
            QStringLiteral("这是一次高质量挥拍，力度与时长协调。保持放松，再练连贯多拍。"),
        };
    case TierGood:
        return {
            QStringLiteral("挥拍基本合格，还有提升空间。建议关注引拍—加速—收拍是否一气呵成。"),
            QStringLiteral("本次挥拍可用，节奏略可再稳定。固定击球点会帮助提高一致性。"),
            QStringLiteral("动作轮廓正确，若感觉球质一般，检查是否发力偏早或偏晚。"),
            QStringLiteral("整体像样，下一拍可刻意放慢 10% 体会完整挥拍路径。"),
            QStringLiteral("识别成功，建议结合教练反馈微调拍面与击球点。"),
        };
    case TierAverage:
        return {
            QStringLiteral("挥拍已记录，但动作一致性一般。先练固定点击球，再追求力量。"),
            QStringLiteral("本次挥拍偏散，建议分解练习：无球挥拍建立路径，再加球。"),
            QStringLiteral("力度与节奏匹配度尚可提升。下一组用 70% 力量追求动作完整。"),
            QStringLiteral("基础框架在建立中，注意侧身与还原，不要只练单手发力。"),
            QStringLiteral("建议选定一种基本技术（高远/杀球/网前）专项练习，避免混练。"),
        };
    default:
        return {
            QStringLiteral("已记录本次挥拍，动作还在磨合期。先保证击球点稳定，再逐步加力。"),
            QStringLiteral("挥拍数据已入库，建议从基本步法和引拍开始系统练习。"),
            QStringLiteral("本次挥拍偏基础，不必急于求成。重复正确动作比大力更重要。"),
            QStringLiteral("可多观察班级优秀学员的挥拍节奏，对照自己的 IMU 数据改进。"),
            QStringLiteral("下一组建议放慢速度，把「引—蹬—转—甩—收」做完整。"),
        };
    }
}

static QStringList strokeAdvice(StrokeKind stroke, ScoreTier tier)
{
    switch (stroke) {
    case StrokeClear:
        return clearAdvice(tier);
    case StrokeDrive:
        return driveAdvice(tier);
    case StrokeLift:
        return liftAdvice(tier);
    case StrokeNet:
        return netAdvice(tier);
    case StrokeServe:
        return serveAdvice(tier);
    case StrokeSmash:
        return smashAdvice(tier);
    default:
        return genericAdvice(tier);
    }
}

static QString powerAddon(StrokeKind stroke, PowerFit fit)
{
    switch (stroke) {
    case StrokeClear:
        if (fit == PowerLow)
            return QStringLiteral(" 补充：高远发力略不足，引拍再充分、蹬转再完整一些。");
        if (fit == PowerHigh)
            return QStringLiteral(" 补充：力量略偏大，注意收拍与落点控制，避免出界。");
        break;
    case StrokeDrive:
        if (fit == PowerLow)
            return QStringLiteral(" 补充：平抽爆发还可加强，缩短引拍、集中加速。");
        if (fit == PowerHigh)
            return QStringLiteral(" 补充：平抽不宜过大力量，保持紧凑快击即可。");
        break;
    case StrokeLift:
        if (fit == PowerLow)
            return QStringLiteral(" 补充：挑球略轻，可稍增加向前送的力量，但仍要柔和。");
        if (fit == PowerHigh)
            return QStringLiteral(" 补充：挑球力度偏大，下一拍再轻触，避免球过高。");
        break;
    case StrokeNet:
        if (fit == PowerLow)
            return QStringLiteral(" 补充：放网力度偏轻，若球未过网可略增加向前送。");
        if (fit == PowerHigh)
            return QStringLiteral(" 补充：放网过力，请刻意减小 30% 力量，追求贴网。");
        break;
    case StrokeServe:
        if (fit == PowerLow)
            return QStringLiteral(" 补充：发球略软，检查击球点是否偏低。");
        if (fit == PowerHigh)
            return QStringLiteral(" 补充：发球略重，稳定性优先，控制力量波动。");
        break;
    case StrokeSmash:
        if (fit == PowerLow)
            return QStringLiteral(" 补充：杀球爆发不足，加强蹬地与甩腕，引拍要开。");
        if (fit == PowerHigh)
            return QStringLiteral(" 补充：杀球力量充足，注意下压角度与落点。");
        break;
    default:
        if (fit == PowerLow)
            return QStringLiteral(" 补充：本次力度偏低，可在动作不变形前提下适度加力。");
        if (fit == PowerHigh)
            return QStringLiteral(" 补充：本次力度偏大，注意控力与稳定性。");
        break;
    }
    return QString();
}

static QString metricAddon(const ClassHitAdviceContext &ctx, quint32 seed)
{
    QStringList parts;
    if (ctx.durationMs >= 0) {
        const double sec = ctx.durationMs / 1000.0;
        if (sec < 0.20) {
            parts << QStringLiteral("挥拍偏快，适合平抽、扑球类动作，注意还原。");
            parts << QStringLiteral("加速段很集中，保持这种短促爆发，但别牺牲击球点。");
        } else if (sec > 0.45) {
            parts << QStringLiteral("挥拍时长偏长，可检查是否引拍过大或收拍过慢。");
            parts << QStringLiteral("动作路径略长，网前小球建议缩短挥拍。");
        } else {
            parts << QStringLiteral("挥拍时长适中，节奏与多数后场技术匹配。");
            parts << QStringLiteral("时长数据正常，可在此基础上微调力度。");
        }
    }
    if (ctx.speedKmh >= 0) {
        if (ctx.speedKmh >= 120) {
            parts << QStringLiteral("估算球速较高，适合进攻型击球，注意落点。");
        } else if (ctx.speedKmh <= 60) {
            parts << QStringLiteral("球速偏温和，网前或过渡球可接受，后场则宜再加送。");
        }
    }
    if (parts.isEmpty())
        return QString();
    return QStringLiteral(" ") + pickOne(parts, seed);
}

} // namespace

QString pickClassHitAiAdvice(const ClassHitAdviceContext &ctx)
{
    const StrokeKind stroke = strokeFromLabel(ctx.hitType);
    const ScoreTier tier = scoreTier(ctx.score);
    const PowerFit power = powerFitFor(stroke, ctx.powerTen);

    const quint32 seed0 = adviceSeed(ctx, 0);
    const quint32 seed1 = adviceSeed(ctx, 1);
    const quint32 seed2 = adviceSeed(ctx, 2);

    QString body = pickOne(strokeAdvice(stroke, tier), seed0);
    if (body.isEmpty())
        body = pickOne(genericAdvice(tier), seed0);

    QString advice = pickOpener(seed1) + body;

    if (power != PowerUnknown && power != PowerIdeal)
        advice += powerAddon(stroke, power);

    advice += metricAddon(ctx, seed2);

    return advice;
}
