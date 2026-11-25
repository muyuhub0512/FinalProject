#include "cpu_common.h"

/* 每个阶段通过 CPU 结构体中的暂存值传递数据。请补全以下六个阶段。 */
void fetch(CPU *cpu)
{
    uint64_t byte0 = read_mem(cpu, cpu->pc, 1);//读取当前指令的第一个字节
    if (cpu->stat != AOK) return;
    cpu->icode = (int)(byte0 >> 4);//读取incode
    cpu->ifun = (int)(byte0 & 15);//读取ifun
    //判断incode和ifun的合法性
    if(cpu->icode>I_POPQ){
        cpu->stat=INS;
        return;}
    {
        if(cpu->icode==I_RRMOVQ||cpu->icode==I_JMP){
        if(cpu->ifun>6){
            cpu->stat=INS;
            return;}}
    else if(cpu->icode==I_ALU){
        if(cpu->ifun>3){
            cpu->stat=INS;
            return;}}
    else{
        if(cpu->ifun!=0){
            cpu->stat=INS;
            return;}}
    }    
    if(cpu->icode==I_HALT){
        cpu->stat=HLT;
        return;}
    //寄存器字节读取与合法性检查
    cpu->rA=RNONE;
    cpu->rB=RNONE;
    int need_reg=0;
    if((cpu->icode>I_NOP&&cpu->icode<I_JMP)||cpu->icode==I_PUSHQ||cpu->icode==I_POPQ){
        need_reg=1;
    }
    if(need_reg){
        uint64_t temp =read_mem(cpu,cpu->pc+1,1);
        if(cpu->stat!=AOK) return;
        cpu->rA=(int)(temp>>4);
        cpu->rB=(int)(temp&15);
        {if(cpu->icode==I_IRMOVQ){
            if(cpu->rA!=RNONE||cpu->rB>14){
                cpu->stat=INS;
                return;}}
        else if(cpu->icode==I_PUSHQ||cpu->icode==I_POPQ){
            if(cpu->rA>14||cpu->rB!=RNONE){
            cpu->stat=INS;
            return;}}
        else{
            if(cpu->rA>14||cpu->rB>14){
                cpu->stat=INS;
                return;}}
    }}
    //立即数读取
    cpu->valC=0;
    int need_valC=0;
    if((cpu->icode>I_RRMOVQ&&cpu->icode<I_ALU)||cpu->icode==I_JMP||cpu->icode==I_CALL){
        need_valC=1;
    }
    if(need_valC){
            cpu->valC=read_mem(cpu,cpu->pc+1+need_reg,8);
            if(cpu->stat!=AOK) return;}
    //计算valP
    cpu->valP=cpu->pc+1+need_reg+8*need_valC;
}

void decode(CPU *cpu)
{
    int srcA=RNONE;
    int srcB=RNONE;//指令执行时需要读取的寄存器
    if(cpu->icode==I_RRMOVQ||cpu->icode==I_RMMOVQ||cpu->icode==I_ALU||cpu->icode==I_PUSHQ){
        srcA=cpu->rA;}
    if(cpu->icode==I_POPQ||cpu->icode==I_RET){
        srcA=RSP;}
    if(cpu->icode==I_RMMOVQ||cpu->icode==I_MRMOVQ||cpu->icode==I_ALU){
        srcB=cpu->rB;}
    if(cpu->icode==I_PUSHQ||cpu->icode==I_POPQ||cpu->icode==I_CALL||cpu->icode==I_RET){
        srcB=RSP;}
    cpu->valA=(srcA==RNONE?0:cpu->reg[srcA]);
    cpu->valB=(srcB==RNONE?0:cpu->reg[srcB]);
}


void execute(CPU *cpu)
{
    cpu->valE = 0;
    cpu->cnd = 0;
    //选择操作数
    uint64_t aluA=0;
    uint64_t aluB=0;//实际参与计算的值
    switch(cpu->icode){
        case(I_ALU):
        case(I_RRMOVQ):
        aluA=cpu->valA;break;
        case(I_IRMOVQ):
        case(I_RMMOVQ):
        case(I_MRMOVQ):
        aluA=cpu->valC;break;
        case(I_CALL):
        case(I_PUSHQ):
        aluA=-8;break;
        case(I_RET):
        case(I_POPQ):
        aluA=8;break;
        default:break;
    }
    switch(cpu->icode){
        case(I_RMMOVQ):
        case(I_MRMOVQ):
        case(I_ALU):
        case(I_CALL):
        case(I_RET):
        case(I_PUSHQ):
        case(I_POPQ):
        aluB=cpu->valB;break;
        default:break;
    }
    //计算valE
    if(cpu->icode==I_ALU){
        switch(cpu->ifun){
            case(0):cpu->valE=aluB+aluA;break;
            case(1):cpu->valE=aluB-aluA;break;
            case(2):cpu->valE=aluB&aluA;break;
            case(3):cpu->valE=aluB^aluA;break;
        }
    }
    else{
        cpu->valE=aluB+aluA;}
    //更新zf，sf
    if(cpu->icode==I_ALU){
        cpu->zf=cpu->valE==0?1:0;
        cpu->sf=cpu->valE>>63;
    }
    //更新of
    int signA=aluA>>63;
    int signB=aluB>>63;
    if(cpu->icode==I_ALU){
        cpu->of=0;
        if(cpu->ifun==0){
            cpu->of=(signA==signB)&&(cpu->sf!=signB);
        }
        if(cpu->ifun==1){
            cpu->of=(signA!=signB)&&(cpu->sf!=signB);
        }
    }
    //计算cnd
    if(cpu->icode==I_RRMOVQ||cpu->icode==I_JMP){
        cpu->cnd=check_cond(cpu, cpu->ifun);
    }
}

void memory_stage(CPU *cpu)
{
    cpu->valM = 0;
    //判断是否需要读写内存
    int needread=0;
    int needwrite=0;
    if(cpu->icode==I_MRMOVQ||cpu->icode==I_POPQ||cpu->icode==I_RET){
        needread=1;
    }
    if(cpu->icode==I_RMMOVQ||cpu->icode==I_PUSHQ||cpu->icode==I_CALL){
        needwrite=1;
    }
    if(!needread&&!needwrite) return;//不需要读写内存的直接结束memory_stage
    //选择访存地址
    uint64_t memaddr=0;
    {if(cpu->icode==I_POPQ||cpu->icode==I_RET) memaddr=cpu->valA;
    else memaddr=cpu->valE;}
    //读入
    if(needread){
        cpu->valM=read_mem(cpu, memaddr, 8);
    }
    //写入
    if(needwrite){
        if(cpu->icode==I_CALL) write_mem(cpu,memaddr,8,cpu->valP);
        else write_mem(cpu,memaddr,8,cpu->valA);
    }
}

void write_back(CPU *cpu)
{
    //确定写回地址
    int dstE=RNONE;
    int dstM=RNONE;//写入的寄存器
    if(cpu->icode==I_RRMOVQ&&cpu->cnd==1) dstE=cpu->rB;
    if(cpu->icode==I_IRMOVQ||cpu->icode==I_ALU) dstE=cpu->rB;
    if(cpu->icode==I_CALL||cpu->icode==I_RET||cpu->icode==I_PUSHQ||cpu->icode==I_POPQ){
        if(cpu->icode==I_POPQ) dstM=cpu->rA;
        dstE=RSP;
    }
    if(cpu->icode==I_MRMOVQ) dstM=cpu->rA;
    //写回
    if(dstE!=RNONE){
        cpu->reg[dstE]=cpu->valE;
    }
    if(dstM!=RNONE){
        cpu->reg[dstM]=cpu->valM;
    }
}

void update_pc(CPU *cpu)
{
    if(cpu->icode==I_CALL||(cpu->icode==I_JMP&&cpu->cnd==1)) cpu->pc=cpu->valC;
    else if(cpu->icode==I_RET) cpu->pc=cpu->valM;
    else cpu->pc=cpu->valP;
}
