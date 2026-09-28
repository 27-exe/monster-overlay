# 路径安全（所有子代理必读）

家目录前缀是 `/home` + `/a27exe`，中间有斜杠。写成 `/homea27exe` 是不存在的路径，
会导致 `cd`/`read_file`/`write_file` 静默失败或 mkdir ��� Permission denied。

**推荐做法**——在 shell 里固化变量，全程只用变量：

```bash
H=/home
U=a27exe
REPO="$H/$U/Projects/games/monster-overlay"
EXP="$H/$U/experiment/monster-overlay-v0.11.0"

# 你的 worktree
WT="$EXP/wt-p1"      # 换成你自己的编号
```

然后所有命令写 `"$WT/src/..."` 而不是手打长绝对路径。
每次落盘前用 `grep -c 'homea27exe' <file>` 自检应为 0。
