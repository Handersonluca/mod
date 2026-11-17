package com.rdzim.memorycheck;

import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;

/** Strict parser shared by the reader and host-side protocol tests. */
final class EspFrameCodec {
    static final class Frame {
        final float[] points,health;
        final String[] names,ids;
        final int bots,unknown;
        final boolean nearValid;
        Frame(float[] p,float[] h,String[] names,String[] ids,int b,int u,boolean v){points=p;health=h;this.names=names;this.ids=ids;bots=b;unknown=u;nearValid=v;}
    }
    static Frame parse(String line){
        if(line.length()>250000)throw new IllegalArgumentException("Frame muito grande");
        String[] f=line.trim().split("\\s+");
        if(f.length<5 || (!f[0].equals("FRAME2")&&!f[0].equals("FRAME3")&&!f[0].equals("FRAME4")))throw new IllegalArgumentException("Cabeçalho inválido");
        boolean identity=f[0].equals("FRAME4"),details=identity||f[0].equals("FRAME3");int wire=identity?38:details?37:33;
        int n=Integer.parseInt(f[1]),b=Integer.parseInt(f[2]),u=Integer.parseInt(f[3]),valid=Integer.parseInt(f[4]);
        if(n<0||n>128||b<0||b>128||u<0||u>128||b+u>128||valid<0||valid>1||(valid==0&&b+u!=0)||f.length!=5+n*wire)throw new IllegalArgumentException("Frame inválido");
        float[] p=new float[n*33],health=new float[n*2];String[] names=new String[n],ids=new String[n];java.util.Arrays.fill(ids,"");java.util.HashSet<String> seen=new java.util.HashSet<>();java.util.Arrays.fill(health,-1);java.util.Arrays.fill(names,"");
        for(int row=0;row<n;row++){
            int base=5+row*wire;
            for(int k=0;k<33;k++){
                float x=Float.parseFloat(f[base+k]);
                if(!Float.isFinite(x)||(k==2?(x<0||x>5000):(x<0||x>1))||(k>=5&&(k-5)%3==0&&x!=0&&x!=1))throw new IllegalArgumentException("Ponto inválido");
                p[row*33+k]=x;
            }
            if(details){
                int hp=Integer.parseInt(f[base+33]),max=Integer.parseInt(f[base+34]),ok=Integer.parseInt(f[base+35]);
                if(ok<0||ok>1||hp<0||max<0||max>65535||hp>max||(ok==1&&max==0)||(ok==0&&(hp!=0||max!=0)))throw new IllegalArgumentException("Vida inválida");
                if(ok==1){health[row*2]=hp;health[row*2+1]=max;}
                names[row]=decodeName(f[base+36]);
                if(identity){String id=f[base+37];if(!id.matches("[1-9a-f][0-9a-f]{0,15}")||!seen.add(id))throw new IllegalArgumentException("Identidade de alvo inválida");ids[row]=id;}
            }
        }
        return new Frame(p,health,names,ids,b,u,valid==1);
    }
    private static String decodeName(String hex){
        if(hex.equals("-"))return "";
        if(hex.isEmpty()||hex.length()>512||hex.length()%2!=0)throw new IllegalArgumentException("Nome inválido");
        byte[] bytes=new byte[hex.length()/2];
        for(int i=0;i<bytes.length;i++){int a=Character.digit(hex.charAt(i*2),16),b=Character.digit(hex.charAt(i*2+1),16);if(a<0||b<0)throw new IllegalArgumentException("Nome inválido");bytes[i]=(byte)((a<<4)|b);}
        try{
            String text=StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString();
            if(text.length()>64)throw new IllegalArgumentException("Nome longo");
            for(int i=0;i<text.length();i++){char ch=text.charAt(i);if(Character.isISOControl(ch)||ch==0x2028||ch==0x2029||(ch>=0x202a&&ch<=0x202e)||(ch>=0x2066&&ch<=0x2069))throw new IllegalArgumentException("Controle no nome");}
            return text;
        }catch(CharacterCodingException e){throw new IllegalArgumentException("Nome UTF-8 inválido",e);}
    }
}
