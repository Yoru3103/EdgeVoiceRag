#!/usr/bin/env python3
"""Render the documentation flowcharts as offline SVGs (Python standard library only)."""
from pathlib import Path
from html import escape

ROOT = Path(__file__).resolve().parent
FONT = "Microsoft YaHei, PingFang SC, Noto Sans CJK SC, Segoe UI, Arial, sans-serif"
C = dict(ink="#16324f", muted="#64748b", line="#cbd5e1", blue="#2563eb",
         purple="#7c3aed", green="#15803d", amber="#c26b09", red="#c24138")

class Canvas:
    def __init__(self, title, subtitle, height, width=1280):
        self.w,self.h=width,height
        self.parts=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
            f'<title id="title">{escape(title)}</title><desc id="desc">{escape(subtitle)}</desc>',
            '<defs>']
        for key in ("blue","purple","green","amber","muted","red"):
            self.parts.append(f'<marker id="arrow-{key}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse"><path d="M 1 1 L 9 5 L 1 9 Z" fill="{C[key]}"/></marker>')
        self.parts += ['</defs>', f'<rect width="{width}" height="{height}" rx="20" fill="#f8fafc"/>',
            f'<style>text{{font-family:{FONT};fill:{C["ink"]}}}.small{{font-size:16px}}.muted{{fill:{C["muted"]}}}</style>']
        self.text(32,49,title,32,bold=True)
        self.text(32,82,subtitle,17,color=C["muted"])
    def rect(self,x,y,w,h,fill="white",stroke=C["line"],rx=12,sw=1):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
    def text(self,x,y,content,size=20,color=None,bold=False,anchor="start",rotate=None):
        attrs=f'x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}"'
        if color: attrs+=f' style="fill:{color}"'
        if bold: attrs+=' font-weight="700"'
        if rotate is not None: attrs+=f' transform="rotate({rotate} {x} {y})"'
        self.parts.append(f'<text {attrs}>{escape(content)}</text>')
    def lines(self,x,y,lines,size=20,line_h=28,color=None,bold=False,anchor="start"):
        for i,line in enumerate(lines): self.text(x,y+i*line_h,line,size,color,bold,anchor)
    def card(self,x,y,w,h,title,body=(),accent="blue",fill="white"):
        self.rect(x,y,w,h,fill)
        self.rect(x,y,5,h,C[accent],C[accent],rx=2,sw=0)
        if isinstance(title,str): title=[title]
        if isinstance(body,str): body=[body]
        lines=len(title)+len(body)
        start=y+(h-(lines-1)*27)/2+7
        self.lines(x+w/2,start,title,21,27,bold=True,anchor="middle")
        self.lines(x+w/2,start+len(title)*27,body,16,24,color=C["muted"],anchor="middle")
    def panel(self,x,y,w,h,title,fill="#eef3f9",accent="blue"):
        self.rect(x,y,w,h,fill,stroke="#dce4ed",rx=16)
        self.text(x+20,y+32,title,22,color=C[accent],bold=True)
    def arrow(self,points,color="blue",dashed=False,label=None,label_at=None,both=False):
        p=" ".join(f'{x},{y}' for x,y in points)
        dash=' stroke-dasharray="7 5"' if dashed else ""
        start=f' marker-start="url(#arrow-{color})"' if both else ""
        self.parts.append(f'<polyline points="{p}" fill="none" stroke="{C[color]}" stroke-width="2"{dash}{start} marker-end="url(#arrow-{color})"/>')
        if label:
            x,y=label_at
            width=max(52,len(label)*16)
            self.rect(x-width/2-5,y-17,width+10,24,"#f8fafc","none",rx=4,sw=0)
            self.text(x,y,label,16,color=C[color],anchor="middle")
    def note(self,x,y,w,lines,fill="#edf3fb",accent="blue"):
        if isinstance(lines,str): lines=[lines]
        h=22+26*len(lines)
        self.rect(x,y,w,h,fill,"none",rx=10,sw=0)
        self.lines(x+16,y+28,lines,17,26,color=C[accent])
    def save(self,name):
        self.parts.append('</svg>')
        (ROOT/(name+".svg")).write_text("\n".join(self.parts),encoding="utf-8")

def overview():
    d=Canvas("01  项目全景","先看三条运行路线；板端本地调用与跨进程联调分开理解",900)
    d.panel(32,118,1216,354,"① 板端 C++ 主线：一个进程内装配",fill="#eef4ff")
    xs=[56,365,674,983]
    titles=["录音与识别","连续会话调度","回答路由","语音输出流水线"]
    bodies=[["ALSA / VAD / ASR"],["每轮回答 + 打断监听"],["设备 Agent / 手册问答"],["分句 → TTS → 播放"]]
    for x,t,b in zip(xs,titles,bodies): d.card(x,180,233,96,t,b)
    for i in range(3): d.arrow([(xs[i]+233,228),(xs[i+1],228)])
    d.card(56,337,233,88,"起声打断",["回答期间监听"],"amber")
    d.card(365,337,233,88,"停止本地输出",["标记请求 / 清队列 / 停播"],"amber")
    d.card(674,337,233,88,"取消当前请求",["按 request_id 传播"],"amber")
    d.arrow([(289,381),(365,381)],"amber",True)
    d.arrow([(598,381),(674,381)],"amber",True)
    d.arrow([(598,381),(635,381),(635,303),(1088,303),(1088,276)],"amber",True,label="停播",label_at=(1140,310))
    d.text(991,397,"虚线：控制路径",17,color=C["amber"])
    d.panel(32,500,592,182,"② C++ 文本客户端",fill="#f1f5f9",accent="muted")
    d.panel(648,500,600,182,"③ Python 原型与服务",fill="#f3efff",accent="purple")
    for x,t in [(52,"终端文字"),(250,"旧分级策略"),(448,"检索 / 服务")]:
        d.card(x,570,158,72,t,accent="muted")
    d.arrow([(210,606),(250,606)],"muted");d.arrow([(408,606),(448,606)],"muted")
    for x,t in [(668,"语音原型"),(866,"TF-IDF RAG"),(1064,"独立生成器")]:
        d.card(x,570,158,72,t,accent="purple")
    d.arrow([(826,606),(866,606)],"purple");d.arrow([(1024,606),(1064,606)],"purple")
    d.panel(32,714,1216,122,"离线资产与评测",fill="#eef6f0",accent="green")
    d.text(56,788,"手册分块  ·  BGE ONNX  ·  文档向量索引  ·  Planner 训练与评测",22,color=C["green"],bold=True)
    d.text(32,876,"模块框不是线程；端口、线程、队列和离线产物分别在后续图中展开。",17,color=C["muted"])
    d.save("01-project-overview")

def routing():
    d=Canvas("02  一个问题如何选择回答方式","路由选择在回答调用路径内执行；具体方法与阈值见图下说明",1060)
    d.card(440,128,400,80,"问题文本 + request_id",["来自 ASR 或调用者"])
    d.arrow([(640,208),(640,250)])
    d.card(440,250,400,100,"外层 Agent 路由",["先检查安全，再看待确认动作和设备意图"])
    d.arrow([(440,300),(220,300),(220,412)],label="非紧急且匹配设备 / Pending",label_at=(220,381))
    d.arrow([(840,300),(1010,300),(1010,412)],label="紧急或普通问答",label_at=(1010,381))
    d.panel(32,412,376,366,"设备 Agent",fill="#eef4ff")
    d.card(62,475,316,100,"规划 → 校验 → 执行",["需要写操作时先请求用户确认"])
    d.card(62,630,316,90,"业务结果或确认提示",["规划 JSON 不直接播报"])
    d.arrow([(220,575),(220,630)])
    d.panel(440,412,808,438,"普通问答 Policy",fill="#f3efff",accent="purple")
    d.card(490,475,708,80,"查询分类 + 按需检索",["BM25 / BGE Dense / Hybrid → 相关性过滤"],"purple")
    d.text(470,601,"策略从以下五种结果中选择：",20,bold=True)
    for x,t,b in [(470,"安全答复","固定文本 + 手册"),(728,"手册直答","首条高置信事实"),(986,"请求澄清","无足够可靠信息")]:
        d.card(x,626,232,82,t,[b],"purple")
    d.card(590,738,252,82,"RAG + LLM",["手册上下文 + 问题"],"purple")
    d.card(870,738,252,82,"纯 LLM",["通用生成提示词"],"purple")
    d.card(440,925,400,80,"统一答复事件",["进入分句、TTS 和播放流水线"],"green")
    d.arrow([(220,778),(220,965),(440,965)],"green")
    d.arrow([(844,850),(844,887),(640,887),(640,925)],"green")
    d.text(32,1040,"Hybrid 依次调用两路检索后做 RRF；当前没有为 BM25、Dense 各建应用线程。",17,color=C["muted"])
    d.save("02-routing-and-retrieval")

def threads():
    d=Canvas("03  五类应用线程如何协作","实线传数据，虚线发控制 / 通知；future 是结果句柄，不是额外线程",920,1440)
    rows=[(150,"T0  主会话","录音、识别与调度","#eef2f7","muted"),
          (280,"T1  回答任务","每轮 std::async","#eaf2ff","blue"),
          (410,"T3  TTS 线程","启动后常驻","#f2ecff","purple"),
          (540,"T4  播放线程","启动后常驻","#eaf7ee","green"),
          (670,"T2  打断监听","每轮 std::async","#fff3e2","amber")]
    for y,title,body,fill,accent in rows:
        d.rect(24,y,1392,108,fill,"none",rx=14,sw=0)
        d.rect(24,y,6,108,C[accent],C[accent],rx=3,sw=0)
        d.text(48,y+43,title,24,color=C[accent],bold=True)
        d.text(48,y+76,body,17,color=C["muted"])
    cards=[
      (310,172,240,"正常录音 → ASR",(),"muted"),
      (610,172,250,"启动 T1、T2",(),"muted"),
      (930,172,380,"轮询 / get / 下一轮",(),"muted"),
      (310,302,240,"路由 / 检索 / 生成",(),"blue"),
      (610,302,250,"回调分句 → 句子队列",(),"blue"),
      (930,302,380,"等待输出完成，再返回",(),"blue"),
      (610,432,250,"取句子 → TTS 合成",(),"purple"),
      (930,432,380,"合成 PCM → 音频队列",(),"purple"),
      (930,562,380,"取音频 → 播放 / 处理 End",(),"green"),
      (310,692,240,"高阈值 VAD 监听",(),"amber"),
      (610,692,250,"起声 → stopPlayback",(),"amber"),
      (930,692,380,"继续录完新问题 → 返回",(),"amber")]
    for x,y,w,t,b,a in cards:d.card(x,y,w,64,t,b,a)
    for y in [204,334,724]:
        d.arrow([(550,y),(610,y)],"muted" if y==204 else ("amber" if y==724 else "blue"))
        d.arrow([(860,y),(930,y)],"muted" if y==204 else ("amber" if y==724 else "blue"))
    d.arrow([(860,464),(930,464)],"purple")
    d.arrow([(735,236),(735,267),(430,267),(430,302)],"blue",label="启动回答",label_at=(580,273))
    d.arrow([(735,366),(735,432)],"blue",label="SentenceTask · 容量 4",label_at=(875,402))
    d.arrow([(1120,496),(1120,562)],"purple",label="AudioTask · 容量 2",label_at=(1235,533))
    d.arrow([(1310,594),(1365,594),(1365,334),(1310,334)],"green",True)
    d.text(1387,532,"End / completed_cv",16,color=C["green"],rotate=-90)
    d.arrow([(610,724),(580,724),(580,350),(550,350)],"amber",True)
    d.text(568,641,"failed / cancel 当前请求",16,color=C["amber"],rotate=-90)
    d.arrow([(735,692),(735,594),(930,594)],"amber",True,label="清队列 / 停播",label_at=(815,585))
    d.note(24,810,1392,["共享 RequestState：shared_ptr 保持存活，mutex 保护字段，completed_cv 通知结束。",
                         "回调不另设分发线程；RKLLM 回调的实际 OS 线程由 SDK 决定，需用线程 ID 核验。"])
    d.save("03-thread-lanes")

def sequence(title,subtitle,roles,events,name,height=1130):
    d=Canvas(title,subtitle,height)
    centers=[140,390,640,890,1140]
    for x,(role,desc,accent) in zip(centers,roles):
        d.card(x-95,120,190,76,role,[desc],accent)
        d.parts.append(f'<line x1="{x}" y1="204" x2="{x}" y2="{height-70}" stroke="#cbd5e1" stroke-width="1.5"/>')
    for number,event in enumerate(events,1):
        y,a,b,label,*rest=event
        color=rest[0] if rest else "blue"
        dashed=rest[1] if len(rest)>1 else False
        x1,x2=centers[a],centers[b]
        if a==b:
            d.arrow([(x1,y),(x1+38,y),(x1+38,y+22),(x1,y+22)],color,dashed)
            d.text(x1+52,y+14,label,18)
        else:
            d.arrow([(x1,y),(x2,y)],color,dashed)
            d.text((x1+x2)/2,y-12,label,18,anchor="middle")
        d.rect(24,y-15,30,30,"#e5edf7","none",rx=15,sw=0)
        d.text(39,y+6,str(number),15,bold=True,anchor="middle")
    d.save(name)
    return d

def normal():
    roles=[("T0 主会话","录音 / 调度","muted"),("T1 回答","路由 / 生成","blue"),("T3 TTS","句子 → PCM","purple"),("T4 播放","PCM → 声卡","green"),("T2 监听","等待起声","amber")]
    events=[(240,0,0,"录音与 ASR","muted"),
            (320,0,1,"启动回答任务"),
            (385,0,4,"同时启动打断监听","amber"),
            (470,1,2,"完整句子入队（SentenceTask）"),
            (545,2,3,"合成 PCM 入队（AudioTask）","purple"),
            (620,3,3,"播放本句","green"),
            (710,1,2,"生成结束：投递 End"),
            (780,2,3,"转发 End 到音频队列","purple"),
            (850,3,1,"完成通知：completed_cv","green",True),
            (925,1,0,"返回本轮结果","blue",True),
            (995,0,4,"取消监听并回收 future","amber")]
    d=sequence("04  正常一轮：生成结束后为什么还要等","代表性时序；不同句子的生成、合成与播放可以重叠",roles,events,"04-normal-turn")
    # Keep the explanatory note outside all message rows.
    d.parts.insert(-1,'<text x="32" y="1106" font-family="'+FONT+'" font-size="17" fill="#64748b">关键：End 随队列传到播放线程，播放线程通知后，回答任务才结束本轮。</text>')
    # save was already called; rewrite with the appended footer.
    (ROOT/"04-normal-turn.svg").write_text("\n".join(d.parts),encoding="utf-8")

def interrupt():
    roles=[("T0 主会话","接收新录音","muted"),("T2 起声监听","VAD 回调","amber"),("输出控制方法","由 T2 直接调用","amber"),("取消后端","按活动请求转发","purple"),("T1 旧回答","等待返回 / 清理","blue")]
    events=[(240,1,1,"检测到用户起声","amber"),
            (315,1,2,"stopPlayback","amber"),
            (390,2,4,"标记 failed，通知 completed","amber"),
            (465,2,2,"清队列 → 停止播放","amber"),
            (545,2,3,"cancel(request_id)","purple"),
            (620,3,3,"清 Pending / 尝试 abort","purple"),
            (700,3,2,"返回取消结果","purple",True),
            (775,2,1,"回调返回，继续录完整语句","amber",True),
            (850,1,0,"新录音就绪，T0 处理 barge 分支","amber",True),
            (925,0,4,"get：等待旧回答任务退出"),
            (1000,0,0,"保存录音 → 下一轮 ASR","muted")]
    d=sequence("05  起声打断：先停输出，再取消后端","输出控制是方法调用，不是新线程；取消不保证旧请求立即退出",roles,events,"05-barge-in",1160)
    footer='<text x="32" y="1125" font-family="'+FONT+'" font-size="17" fill="#64748b">任一分支取得成功录音均保留；已观察到起声时，等待新语句录完。</text>'
    d.parts.insert(-1,footer)
    (ROOT/"05-barge-in.svg").write_text("\n".join(d.parts),encoding="utf-8")

def agent():
    d=Canvas("06  Agent：一次写操作如何跨轮确认","Planner / Executor / Tool 是调用层次，不是三条独立线程",980)
    d.panel(32,130,568,646,"本轮：提出操作，等待确认",fill="#eef4ff")
    d.panel(680,130,568,646,"下一轮：处理确认，继续工作流",fill="#eef7f0",accent="green")
    left=[("用户提出设备任务","例如：温度超过阈值就打开空调"),
          ("Planner 决定下一步","Rule 或 LLM，输出结构化动作"),
          ("Executor 检查工具","白名单 / 参数 / 是否需要确认"),
          ("保存 Pending，播报确认提示","此时还没有执行写操作")]
    right=[("用户说“确认 / 取消”","新 request_id，同一 session_id"),
           ("检查 Pending 与确认有效期","超时或取消：清理，不执行写工具"),
           ("确认后执行保存的工具","条件工具此时重新读取环境并判断"),
           ("Observation → Planner","继续规划或生成最终业务答复")]
    for y,(t,b) in zip([214,350,486,622],left): d.card(64,y,504,92,t,[b])
    for y,(t,b) in zip([214,350,486,622],right): d.card(712,y,504,92,t,[b],"green")
    for y in [306,442,578]:
        d.arrow([(316,y),(316,y+44)])
        d.arrow([(964,y),(964,y+44)],"green")
    d.text(980,470,"仅有效确认",16,color=C["green"])
    d.arrow([(568,668),(640,668),(640,260),(712,260)],"amber",True)
    d.text(628,543,"跨语音轮次",17,color=C["amber"],rotate=-90)
    d.note(32,814,1216,["只读工具可直接执行；遇到最终答复可提前结束；循环仍受 maximum_steps 限制。",
                        "Linux IIO 读取真实温湿度；空调 / LED 当前仍为内存状态，取消不会回滚已执行动作。"],fill="#fff3e2",accent="amber")
    d.save("06-agent-workflow")

def network():
    d=Canvas("07  跨进程调用：数据与取消分开走","端口是默认值；板端本地路线不要求启动这些服务",990)
    cols=[(32,300,"客户端进程","#eef2f7","muted"),(412,380,"Python RAG 进程","#f2ecff","purple"),(872,376,"C++ LLM 进程","#eaf2ff","blue")]
    for x,w,t,fill,a in cols:d.panel(x,130,w,684,t,fill,a)
    items=[
      (56,230,252,"普通请求","文字 / JSON 结果","muted"),
      (436,230,332,"5556 · 普通 RAG","REP · 主循环","purple"),
      (896,230,328,"8899 · 普通 LLM","REP · 主循环","blue"),
      (56,430,252,"流式请求","逐段接收答复事件","blue"),
      (436,430,332,"5557 · RAG 流","ROUTER · 同一主循环","purple"),
      (896,430,328,"8900 · LLM 流","ROUTER · 同一主循环","blue"),
      (56,658,252,"取消请求","独立控制通道","amber"),
      (436,658,332,"5558 · RAG 控制","REP · 独立控制线程","amber"),
      (896,658,328,"8901 · LLM 控制","REP · 独立控制线程","amber")]
    for x,y,w,t,b,a in items:d.card(x,y,w,92,t,[b],a)
    for y in [276,476]:
        d.arrow([(308,y),(436,y)],"blue",both=True)
        d.arrow([(768,y),(896,y)],"purple",both=True)
    d.arrow([(308,704),(436,704)],"amber",True)
    d.arrow([(768,704),(896,704)],"amber",True)
    d.text(372,201,"请求 / 响应",16,color=C["muted"],anchor="middle")
    d.text(833,201,"选 ZMQ 生成器",16,color=C["muted"],anchor="middle")
    d.text(436,586,"RAG：检索 → 生成 / 转发",18,color=C["purple"])
    d.text(896,586,"两个数据端口共享一个模型",18,color=C["blue"])
    d.note(32,846,1216,["旧 C++ RAG 使用 5555；文本 CLI 也可直接请求 8899，另有 Python 普通 LLM 替代服务。",
                         "8901 只匹配活动流请求；双端口不代表模型并发，控制服务也不能保证取消普通非流式生成。"])
    d.save("07-processes-and-network")

def offline():
    d=Canvas("08  离线资产：先生成，再由运行时加载","检索资产和 Planner 训练是两条独立准备链；都不是每轮语音中临时执行",1020)
    d.panel(32,130,592,756,"手册与向量索引",fill="#eef4ff")
    d.panel(656,130,592,756,"Planner 训练与评测",fill="#eef7f0",accent="green")
    d.card(56,212,248,80,"手册 + 同义词")
    d.card(352,212,248,80,"BGE 基础模型")
    d.card(56,352,248,86,"分块 → chunks.json",["build_index.py"])
    d.card(352,352,248,86,"导出 ONNX 与分词器",["export_bge_onnx.py"])
    d.arrow([(180,292),(180,352)]);d.arrow([(476,292),(476,352)])
    d.card(80,514,496,86,"离线计算文档向量",["embeddings.f32 + index_meta.json"])
    d.arrow([(180,438),(180,475),(328,475),(328,514)])
    d.arrow([(476,438),(476,475),(328,475)])
    d.card(80,660,496,86,"校验模型、索引与指纹",["verify_dense_index.py"])
    d.arrow([(328,600),(328,660)])
    d.text(86,824,"启动时：RetrieverRuntime 加载资产",20,bold=True)
    d.arrow([(328,746),(328,784)])
    right=[("生成并划分 JSONL 数据","train / validation / heldout"),
           ("校验数据与动作格式","validate_dataset.py"),
           ("QLoRA 微调","train_qlora.py + 基础模型"),
           ("保存 adapter 与训练指标","final_adapter / train_metrics.json"),
           ("基础模型与 adapter 对照评测","evaluate_planner.py")]
    ys=[202,330,458,586,714]
    for y,(t,b) in zip(ys,right):d.card(688,y,528,86,t,[b],"green")
    for y in [288,416,544,672]:d.arrow([(952,y),(952,y+42)],"green")
    d.note(32,916,1216,["adapter 尚无已验证的“合并 → RKLLM 转换 → 板端对齐”闭环，不会自动变成当前板端模型。"],fill="#fff3e2",accent="amber")
    d.save("08-offline-assets")


def performance_workflow():
    d=Canvas("09  RK3588 性能测试路线","先让每个环节可用，再用固定输入比较；板端后续步骤由使用者操作",1010)
    d.panel(32,116,1216,208,"① 固定实验身份 · WSL → RK3588",fill="#eef4ff")
    for x,t,b in [(56,"Release 交叉构建",["源码 / 配置 / SDK / 模型 hash"]),(464,"独立目录部署",["ARM ELF + 动态库 + 资产校验"]),(872,"保留实验清单",["build-manifest + 输入 / 线程"])]:
        d.card(x,186,352,96,t,b)
    d.arrow([(408,234),(464,234)]);d.arrow([(816,234),(872,234)])
    d.arrow([(640,324),(640,346),(328,346),(328,366)])
    d.arrow([(640,346),(948,346),(948,366)])
    d.panel(32,366,592,328,"② 固定查询 · 先建立数值对照",fill="#eef6f0",accent="green")
    d.card(56,432,544,88,"BM25 / Dense / Hybrid",["29 条查询 × 30 遍；串行执行"],"green")
    d.card(56,564,544,88,"Dense 显式分段",["prepare → ORT Run → post → index / mapping"],"green")
    d.arrow([(328,520),(328,564)],"green")
    d.panel(648,366,600,328,"③ 真实语音 · 功能通过后测时延",fill="#f3efff",accent="purple")
    d.card(672,432,552,88,"录音 → 回放 → ASR → TTS",["先确认声卡参数；失败保留日志"],"purple")
    d.card(672,564,552,88,"手册 / RKLLM / Agent / 打断",["PERF_JSON + 听感 + 失败 / 取消计数"],"purple")
    d.arrow([(948,520),(948,564)],"purple")
    d.arrow([(328,694),(328,737),(640,737),(640,780)],"green")
    d.arrow([(948,694),(948,737),(640,737)],"purple")
    d.card(270,780,740,96,"归档原始样本 → 核对清单 → 阅读 P50 / P95 / RSS",["记录温度 / 频率；缺失观测保留缺失，不补成 0"],"blue")
    d.note(32,912,1216,["检索基准：BGE 使用 CPU；NPU / RKLLM 需在语音生成路径另行测量。",
        "当前记录：固定查询已完成；语音曾因 ALSA 参数失败，尚无成功语音性能基线。"],fill="#fff3e2",accent="amber")
    d.save("09-rk3588-performance-workflow")

def performance_points():
    d=Canvas("10  性能观测：调用与跨线程边界","同一 request_id 关联事件；横向箭头是数据流，阶段可能重叠",990)
    d.panel(32,116,1216,212,"① 回答调用路径 · T2 回答线程",fill="#eef4ff")
    for x,t,b in [(56,"检索",["BM25 / BGE / Hybrid"]),(464,"策略 / 生成",["手册直答 / RKLLM"]),(872,"文本分句",["首文本 → 句子片段"])]:
        d.card(x,184,352,96,t,b)
    d.arrow([(408,232),(464,232)]);d.arrow([(816,232),(872,232)])
    d.panel(32,370,1216,218,"② 跨线程输出 · 有界队列",fill="#f3efff",accent="purple")
    for x,w,t,b in [(56,270,"sentence_queue",["T2 push → T3 pop"]),(366,240,"T3 · TTS",["首音频 / 合成累计"]),(646,270,"audio_queue",["T3 push → T4 pop"]),(956,268,"T4 · 播放",["首播 / 播放累计"])]:
        d.card(x,439,w,92,t,b,"purple")
    for a,b in [(326,366),(606,646),(916,956)]:d.arrow([(a,485),(b,485)],"purple")
    d.arrow([(1048,280),(1048,347),(640,347),(640,414),(191,414),(191,439)],"purple")
    d.note(56,609,1168,["两路队列：容量满时 push 等待次数 / 累计等待；成功入队时采样共享深度峰值。",
        "这些等待与 TTS / 播放可能重叠；不包含消费者等空队列，也不是任务驻留时间。"])
    d.panel(32,722,592,170,"③ 独立 Dense profile 工具",fill="#eef6f0",accent="green")
    d.lines(56,794,["prepare → ORT Session::Run → postprocess",
                       "→ index search → mapping；入口另记 total"],19,32)
    d.panel(648,722,600,170,"④ 汇总与资源 · 不同统计范围",fill="#fff3e2",accent="amber")
    d.lines(672,794,["语音：AnswerCompleted → PERF_JSON → 汇总",
                        "检索：逐 run/case 样本 + 原生命令峰值 RSS"],19,32)
    d.text(32,948,"P50 / P95 逐列计算，不能相加；取消 / 失败轮次需要单独保留状态。",20,color=C["muted"])
    d.save("10-performance-measurement-points")


def dht11_architecture():
    d=Canvas("11  DHT11 驱动框图","从应用读取到硬件边沿：缓存、测量、硬中断与结果发布各有职责",1240,1440)
    d.panel(32,116,1376,174,"① 用户态：两种调用入口",fill="#eef4ff")
    d.card(160,174,440,84,"C++ 设备 Agent",["温湿度工具 → LinuxVehicleDevice"])
    d.card(840,174,440,84,"诊断程序 / shell",["探针脚本或 cat 读取 IIO 属性"])
    d.arrow([(380,258),(380,312),(720,312),(720,354)])
    d.arrow([(1060,258),(1060,312),(720,312)])
    d.card(400,354,640,114,"Linux IIO：read_raw + mutex",
           ["按 name=edge_dht11 定位；温度 / 湿度 input",
            "成功缓存 2 秒：命中直接返回，失效进入真实测量"])
    d.panel(32,490,1376,422,"② 驱动：进程上下文与硬中断协作",fill="#f3efff",accent="purple")
    d.arrow([(720,468),(720,546),(362,546),(362,568)])
    d.card(72,568,580,128,"进程上下文 · edge_dht11_measure",
           ["CPU QoS 请求 + IRQ 注册",
            "开漏拉低 18～20 ms → 释放 → 等待 completion"],"purple")
    d.card(788,568,580,128,"硬中断上下文 · IRQ handler",
           ["GPIO 上升沿 / 下降沿触发",
            "时间戳 + 电平写入 edges[]；达标后通知"],"purple")
    d.arrow([(652,632),(788,632)],"purple",label="硬件边沿触发",label_at=(720,616))
    d.arrow([(1078,696),(1078,732),(540,732),(540,696)],
            "purple",True,label="complete 唤醒等待者",label_at=(835,726))
    d.arrow([(362,696),(362,778)],"green")
    d.card(72,778,580,94,"采集结束 → 清理 → 严格解码",
           ["free_irq → 移除 QoS → 电平 / 40 位 / 校验和 / 量程"],"green")
    d.card(788,778,580,94,"缓存发布 / IIO 返回",
           ["完整校验成功才发布；失败返回 errno"],"green")
    d.arrow([(652,825),(788,825)],"green")
    d.arrow([(1368,825),(1424,825),(1424,411),(1040,411)],
            "green",label="返回数值 / errno",label_at=(1220,395))
    d.panel(32,950,1376,208,"③ 内核框架与实物依赖",fill="#eef6f0",accent="green")
    d.card(56,1018,400,94,"CPU latency QoS",["测量时添加 0 µs 请求；结束移除"],"green")
    d.card(520,1018,400,94,"GPIO / IRQ 框架",["设备树指定 GPIO；当前 GPIO0_C6"],"green")
    d.card(984,1018,400,94,"DHT11 模块",["JP5-10 DATA；3.3V 上拉；共地"],"green")
    d.arrow([(920,1065),(984,1065)],"green",both=True)
    d.text(32,1200,"框表示职责；IRQ 不是新建的用户态线程。具体时序见图12，异常与修订见图13。",18,color=C["muted"])
    d.save("11-dht11-driver-architecture")

def dht11_timing():
    d=Canvas("12  DHT11 一次测量：调用与中断时序","缓存未命中路径；按先后顺序示意，不按真实时长比例绘制",1190,1440)
    xs=[200,522,866,1237]
    d.rect(32,310,1376,630,"#fff8e9","#f3dfb8",rx=16)
    d.text(1378,339,"QoS 持有区间",18,color=C["amber"],bold=True,anchor="end")
    for x in xs:
        d.parts.append(f'<line x1="{x}" y1="206" x2="{x}" y2="1038" stroke="{C["line"]}" stroke-width="2" stroke-dasharray="6 5"/>')
    d.rect(512,354,20,566,"#ffe0a3","#e8bd6e",rx=4)
    d.rect(1227,426,20,430,"#ddd0ff","#b89de9",rx=4)
    for x,w,t in [(56,288,"用户进程 · measure"),(392,260,"CPU latency QoS"),
                  (716,300,"GPIO + DHT11"),(1090,294,"GPIO 硬中断")]:
        d.card(x,132,w,74,t,accent="purple" if x==1090 else "blue")
    d.card(56,238,288,54,"缓存失效 → 测量")
    d.note(392,236,992,["缓存命中直接返回：不创建 QoS 请求，也不启动总线测量。"])
    d.arrow([(200,354),(522,354)],"amber",label="① 添加 0 µs 请求",label_at=(361,342))
    d.arrow([(200,426),(1237,426)],"purple",label="② request_irq：在起始信号之前就绪",label_at=(718,414))
    d.arrow([(200,504),(866,504)],label="③ 开漏拉低 DATA",label_at=(533,492))
    d.card(726,540,280,72,["起始低电平","18～20 ms"],accent="blue")
    d.text(1378,580,"起始沿不入帧",17,color=C["purple"],anchor="end")
    d.arrow([(200,650),(866,650)],label="④ 释放 DATA，等待 completion",label_at=(533,638))
    d.arrow([(866,716),(1237,716)],"purple",label="⑤ 记录边沿时间戳 / 电平",label_at=(1052,704))
    d.arrow([(1237,786),(200,786)],"purple",True,label="⑥ complete：唤醒等待者",label_at=(718,774))
    d.arrow([(200,856),(1237,856)],"green",label="⑦ free_irq：同步并结束采集",label_at=(718,844))
    d.arrow([(200,920),(522,920)],"amber",label="⑧ 移除本次 QoS 请求",label_at=(361,908))
    d.card(56,962,288,74,"⑨ 严格校验后返回",["成功才更新温湿度缓存"],"green")
    d.note(32,1060,1376,["统一清理：IRQ申请失败、拉低失败、超时、中断、校验失败，均移除本次请求。",
                         "条带表示资源持有区间，不是 CPU 执行占比；失败可能等待到驱动超时。"],
           fill="#edf3fb",accent="blue")
    d.text(32,1160,"IRQ handler 只记录与通知；可睡眠的等待、QoS 管理、解析和打印均在进程上下文完成。",18,color=C["muted"])
    d.save("12-dht11-measurement-timing")

def dht11_faults():
    d=Canvas("13  DHT11 异常：证据、原因与修订","区分采集超时、GPIO接口错误与帧判定；按用户后续复测反馈记录当前状态",1250,1440)
    d.rect(32,116,1376,72,"#eaf0f7","none")
    d.text(56,162,"现象 / 实测",23,bold=True)
    d.text(370,162,"原因与证据边界",23,bold=True)
    d.text(930,162,"修订 / 当前状态",23,bold=True)
    rows=[
      (214,190,["边沿不足而超时","连续电平不交替"],["例：34～81沿，记录失真"],
       "微秒级边沿未被可靠记录",
       ["IRQ 日志证实记录异常","原0.2.1：普通0/5，外部QoS 5/5",
        "支持 CPU 空闲 / 系统时序影响","尚非唯一物理根因证明"],
       "0.2.2：测量窗口 QoS",
       ["测量前添加 0 µs，采集后移除","成功、超时、中断、错误均清理",
        "本轮10/10；用户后测反馈正常"],"purple"),
      (432,190,"拉低 GPIO 失败 -EIO",["0.2.0 引入的接口错误"],
       "IRQ 已启用时调用 direction_output",
       ["内核先检查 IRQ 方向保护","开漏标志不会跳过该检查"],
       "0.2.1：改用开漏 set_value",
       ["逻辑0拉低，逻辑1释放","使用框架接口，不改内部标志",
        "后续未再出现方向保护错误"],"blue"),
      (650,190,"82 沿直接报超时",["可能过早拒绝完整数据帧"],
       "门槛过严；要区分缺少什么",
       ["缺前导仍可能保留完整40位","数据段缺失不能凭计数补回",
        "当前81沿失败帧含电平失真"],
       "完整帧校验后再接受",
       ["82沿可尝试解码，仍校验电平","完整40位 + 校验和 + 数值范围",
        "计数接近不等于帧有效"],"green"),
      (868,224,"当前验证结果",["本轮10次读取均成功","后续复测用户反馈正常"],
       "从外部对照到驱动自管理",
       ["0.2.1：外部QoS对照0/5→5/5","0.2.2：仅测量窗口持有约束",
        "后续复测按用户反馈正常","未提供后测数量，不补写成功率"],
       "继续板端主线",
       ["已结束本轮传感器专项诊断","真实温湿度工具 → Agent联调",
        "语音录放音与性能分别验证"],"green")]
    for y,h,lt,lb,mt,mb,rt,rb,color in rows:
        d.card(56,y,270,h,lt,lb,color)
        d.card(370,y,500,h,mt,mb,color)
        d.card(930,y,454,h,rt,rb,color)
        d.arrow([(326,y+h/2),(370,y+h/2)],color)
        d.arrow([(870,y+h/2),(930,y+h/2)],color)
    d.note(32,1132,1376,["成功率来自用户提供的板端日志，不是智能体新增的板端实测。",
                         "本轮已观察到超时改善，用户后续复测反馈正常；Agent 与真实语音链路另行验证。"],
           fill="#fff3e2",accent="amber")
    d.save("13-dht11-faults-and-fixes")


if __name__ == "__main__":
    for draw in (overview,routing,threads,normal,interrupt,agent,network,offline,performance_workflow,performance_points,dht11_architecture,dht11_timing,dht11_faults): draw()
    print("Rendered 13 documentation SVGs.")
