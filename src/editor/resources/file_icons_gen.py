from PIL import Image
N=32; SCALE=4; COLS=8
OUT=(52,46,62,255)
PAPER=(251,248,241,255); PSHADE=(228,222,210,255); FOLD=(206,198,184,255)
def new(): return Image.new('RGBA',(N,N),(0,0,0,0))
def px(im,x,y,c):
    if 0<=x<N and 0<=y<N: im.putpixel((x,y),c)
def rect(im,x0,y0,x1,y1,c):
    for y in range(y0,y1+1):
        for x in range(x0,x1+1): px(im,x,y,c)
def outline_rect(im,x0,y0,x1,y1,fill,out=OUT,round_=True):
    rect(im,x0+1,y0+1,x1-1,y1-1,fill)
    for x in range(x0+(1 if round_ else 0),x1+(0 if round_ else 1)): px(im,x,y0,out); px(im,x,y1,out)
    for y in range(y0+(1 if round_ else 0),y1+(0 if round_ else 1)): px(im,x0,y,out); px(im,x1,y,out)
FONT={ # 3x5
 'J':["001","001","001","101","010"],'S':["011","100","010","001","110"],'P':["110","101","110","100","100"],
 'Y':["101","101","010","010","010"],'C':["011","100","100","100","011"],'+':["000","010","111","010","000"],
 'X':["101","101","010","101","101"],'M':["101","111","111","101","101"],'L':["100","100","100","100","111"],
 'T':["111","010","010","010","010"],'{':["011","010","100","010","011"],'}':["110","010","001","010","110"],
 'A':["010","101","111","101","101"],'a':["000","011","101","101","011"],'H':["101","101","111","101","101"],
 'D':["110","101","101","101","110"],'I':["111","010","010","010","111"],'N':["101","111","111","111","101"],
 'G':["011","100","101","101","011"],'O':["010","101","101","101","010"],'V':["101","101","101","101","010"],
 'B':["110","101","110","101","110"],'R':["110","101","110","101","101"],
}
def text(im,s,x,y,c):
    for ch in s:
        g=FONT[ch]
        for j,row in enumerate(g):
            for i,b in enumerate(row):
                if b=='1': px(im,x+i,y+j,c)
        x+=4
def page(im, badge=None, badge_col=None, label=None, label_col=(255,255,255,255)):
    # page with a folded corner (top right)
    x0,y0,x1,y1=6,3,25,28; F=6
    rect(im,x0+1,y0+1,x1-1,y1-1,PAPER)
    rect(im,x0+1,y1-3,x1-1,y1-1,PSHADE)
    for y in range(y0,y0+F):                     # cut corner : transparent above the diagonal
        for x in range(x1-F+(y-y0)+1,x1+1): px(im,x,y,(0,0,0,0))
    for x in range(x0+1,x1-F+1): px(im,x,y0,OUT)   # top
    for y in range(y0+1,y1): px(im,x0,y,OUT)       # left
    for y in range(y0+F,y1): px(im,x1,y,OUT)       # right
    for x in range(x0+1,x1): px(im,x,y1,OUT)       # bottom
    for i in range(F+1): px(im,x1-F+i,y0+i,OUT)    # diagonal
    for i in range(1,F):                           # fold
        for x in range(x1-F+1,x1-F+i): px(im,x,y0+i,FOLD)
    for y in range(y0+1,y0+F): px(im,x1-F,y,OUT)
    for x in range(x1-F,x1): px(im,x,y0+F,OUT)
    if badge_col:
        bx0,by0,bx1,by1=3,17,3+max(12,len(label)*4+3),25
        outline_rect(im,bx0,by0,bx1,by1,badge_col)
        rect(im,bx0+1,by1-1,bx1-1,by1-1,shade(badge_col,0.82))
        if label: text(im,label,bx0+2,by0+2,label_col)
def shade(c,f): return (int(c[0]*f),int(c[1]*f),int(c[2]*f),255)
def lines(im,y0,n,c=(170,164,155,255),x0=10,x1=21):
    for i in range(n):
        w = x1 - (3 if i%3==2 else 0)
        rect(im,x0,y0+i*3,w,y0+i*3,c)

icons=[]
# 0 Folder (lynx ears on the tab)
im=new(); BODY=(242,180,76,255); BACK=(214,146,54,255); HI=(252,212,122,255); FO=(120,76,32,255); PINK=(240,150,140,255)
outline_rect(im,3,8,28,26,BACK,FO)
rect(im,4,6,12,8,BACK)
for x in range(4,13): px(im,x,5,FO)
px(im,3,6,FO); px(im,3,7,FO); px(im,13,6,FO); px(im,14,7,FO)
for a in (4,8):          # two lynx ears (with their tuft) on the tab
    px(im,a+2,1,FO); px(im,a+2,2,FO)
    px(im,a+1,3,FO); px(im,a+2,3,PINK); px(im,a+3,3,FO)
    px(im,a,4,FO); px(im,a+1,4,BACK); px(im,a+2,4,PINK); px(im,a+3,4,BACK); px(im,a+4,4,FO)
outline_rect(im,3,12,28,26,BODY,FO)
rect(im,4,13,27,13,HI)
rect(im,4,25,27,25,shade(BODY,0.88))
icons.append(('Folder',im))
# 1 File generic : paw print
im=new(); page(im)
P=(190,150,120,255)
rect(im,14,18,17,20,P); px(im,13,19,P); px(im,18,19,P); rect(im,14,21,17,21,P)
for (x,y) in ((11,15),(14,13),(17,13),(20,15)): rect(im,x,y,x+1,y+1,P)
icons.append(('File',im))
# 2 Text
im=new(); page(im); lines(im,10,6); icons.append(('Text',im))
# 3 JS
im=new(); page(im,True,(247,212,72,255),"JS",OUT); lines(im,8,3); icons.append(('Script',im))
# 4 Python
im=new(); page(im,True,(74,124,190,255),"PY",(255,224,96,255)); lines(im,8,3); icons.append(('Python',im))
# 5 C++
im=new(); page(im,True,(124,110,214,255),"C+"); lines(im,8,3); icons.append(('Cpp',im))
# 6 Data json
im=new(); page(im,True,(96,168,132,255),"{}"); lines(im,8,3); icons.append(('Data',im))
# 7 Level (map with pin)
im=new(); MAP=(150,206,170,255); WATER=(140,190,226,255)
outline_rect(im,4,7,27,26,MAP)
rect(im,5,19,26,25,WATER); rect(im,5,18,12,18,WATER)
for x in range(5,27,3): px(im,x,12,(110,170,130,255))
# pin
PIN=(226,84,84,255)
rect(im,17,6,21,10,PIN); outline_rect(im,16,5,22,11,PIN)
px(im,19,8,(255,255,255,255)); px(im,19,12,OUT); px(im,19,13,OUT); px(im,18,12,PIN); px(im,20,12,PIN)
icons.append(('Level',im))
# 8 Image (sun + mountains)
im=new(); SKY=(170,212,240,255)
outline_rect(im,3,6,28,26,SKY)
rect(im,22,9,24,11,(255,214,90,255)); px(im,23,8,(255,214,90,255)); px(im,21,10,(255,214,90,255))
G1=(98,170,96,255); G2=(70,140,80,255)
for x in range(4,28):
    h1=max(0,8-abs(x-11)); h2=max(0,6-abs(x-20)//1)
    for y in range(25-h1,26): px(im,x,y,G2)
    for y in range(25-h2,26):
        if h2>0: px(im,x,y,G1)
icons.append(('Image',im))
# 9 Sound (note)
im=new(); page(im); N1=(150,100,204,255)
rect(im,13,19,16,22,N1); rect(im,19,17,22,20,N1)
rect(im,16,9,16,21,N1); rect(im,22,8,22,19,N1); rect(im,16,8,22,10,N1)
icons.append(('Sound',im))
# 10 Font
im=new(); page(im); text(im,"A",11,11,(200,84,118,255)); text(im,"a",16,13,(200,84,118,255))
rect(im,10,22,21,22,(200,84,118,255)); icons.append(('Font',im))
# 11 Widget (UI window)
im=new(); WB=(92,150,228,255)
outline_rect(im,3,6,28,26,(245,247,252,255))
rect(im,4,7,27,10,WB)
for x in (24,26): px(im,x,8,(255,255,255,255))
outline_rect(im,7,14,18,18,(210,224,246,255),WB)
outline_rect(im,7,20,24,24,WB,shade(WB,0.7)); rect(im,9,22,14,22,(255,255,255,255))
icons.append(('Widget',im))
# 12 Anim graph (states + arrow)
im=new(); AG=(238,124,108,255); AG2=(255,190,120,255)
outline_rect(im,3,7,13,15,AG); outline_rect(im,18,17,28,25,AG2)
for i in range(6): px(im,12+i,15+i, OUT)
px(im,17,20,OUT); px(im,16,20,OUT); px(im,17,19,OUT)
rect(im,5,10,11,10,(255,255,255,255)); rect(im,20,20,26,20,(255,255,255,255))
icons.append(('AnimGraph',im))
# 13 Behavior tree
im=new(); BT=(90,178,112,255); BT2=(150,206,120,255)
outline_rect(im,12,3,19,9,BT)
outline_rect(im,3,19,10,25,BT2); outline_rect(im,12,19,19,25,BT2); outline_rect(im,21,19,28,25,BT2)
rect(im,15,10,16,13,OUT); rect(im,6,14,25,14,OUT); rect(im,6,15,6,18,OUT); rect(im,15,15,16,18,OUT); rect(im,25,15,25,18,OUT)
icons.append(('BehaviorTree',im))
# 14 Voxels (isometric-ish cube)
im=new(); TOP=(126,212,96,255); L=(150,106,70,255); R=(118,82,54,255)
outline_rect(im,5,9,26,26,L)
rect(im,16,10,25,25,R); rect(im,6,10,25,13,TOP); rect(im,6,14,25,14,shade(TOP,0.8))
for y in (18,22): rect(im,7,y,9,y,shade(L,1.2)); rect(im,19,y+1,21,y+1,shade(R,1.25))
icons.append(('Voxels',im))
# 15 Config (gear on page)
im=new(); page(im); GC=(120,128,140,255)
outline_rect(im,12,12,20,20,GC,shade(GC,0.6)); 
for (x,y) in ((15,10),(16,10),(15,22),(16,22),(10,15),(10,16),(22,15),(22,16)): px(im,x,y,shade(GC,0.6)); 
rect(im,15,15,17,17,PAPER)
icons.append(('Config',im))
# 16 Code generic (glsl, h...)
im=new(); page(im,True,(84,92,110,255),"<>" if False else "{}"); lines(im,8,3); icons.append(('Code',im))
# 17 Level xml-other : Markup
im=new(); page(im,True,(226,124,64,255),"XML"); lines(im,8,3); icons.append(('Markup',im))

rows=(len(icons)+COLS-1)//COLS
atlas=Image.new('RGBA',(COLS*N*SCALE,rows*N*SCALE),(0,0,0,0))
for i,(name,im) in enumerate(icons):
    atlas.paste(im.resize((N*SCALE,N*SCALE),Image.NEAREST),((i%COLS)*N*SCALE,(i//COLS)*N*SCALE))
atlas.save('./file_icons.png')
# preview
pv=Image.new('RGBA',atlas.size,(214,206,190,255)); pv.alpha_composite(atlas); pv.save('./preview.png')
print([n for n,_ in icons], atlas.size)
