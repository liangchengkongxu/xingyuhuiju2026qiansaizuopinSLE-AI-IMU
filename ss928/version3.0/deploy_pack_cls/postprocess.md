# 分类后处理

1. 读取 NPU 输出 `output0`，shape `[1, 5]`
2. 对 5 个 logits 做 softmax
3. 取最大概率的 index 作为类别 id
4. 用 label_map.txt 查类别名

## 类别顺序

0 fangwang
1 gaoyuan
2 pingchou
3 shaqiu
4 tiaoqiu

## 建议阈值

- conf >= 0.5 才上报结果
- 连续 N 帧（如 10 帧）同一类别再输出，减少抖动

## softmax 公式

p_i = exp(x_i) / sum(exp(x_j))

注意数值稳定：先减去 max(x) 再 exp
