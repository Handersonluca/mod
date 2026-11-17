package com.rdzim.memorycheck;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** One root reader session; generation checks discard callbacks after stop/restart. */
public final class EspSession {
    public interface Listener {void frame(float[] points,long arrived);void status(String value,String log);void aimStatus(String value);void richFrame(EspFrameCodec.Frame frame,long arrived);}
    private final File binary;
    private final Listener listener;
    private final Handler main=new Handler(Looper.getMainLooper());
    private static final class UiFrame {
        final int generation; final long arrived;
        final float[] simple; final EspFrameCodec.Frame rich;
        UiFrame(int generation,long arrived,float[] simple,EspFrameCodec.Frame rich){
            this.generation=generation;this.arrived=arrived;this.simple=simple;this.rich=rich;
        }
    }
    private final LatestFramePump<UiFrame> frames;
    private volatile int generation;
    private volatile Process process;
    private volatile Thread worker;
    private volatile boolean desiredAim;
    private volatile String lastAimDiagnostic="";
    private volatile int selectedFov=130,selectedVisuals=5,selectedTarget;
    private final ExecutorService commands=Executors.newSingleThreadExecutor();
    private final ReaderControlPackets controlPackets=new ReaderControlPackets();
    private final LatestCommandPump controlPump=new LatestCommandPump(commands,this::writeLatestControl);
    private final java.util.concurrent.ScheduledExecutorService heartbeat=Executors.newSingleThreadScheduledExecutor();
    public EspSession(Context context,Listener callback){binary=new File(context.getApplicationInfo().nativeLibraryDir,"libmemory_v311.so");listener=callback;
        frames=new LatestFramePump<>(
        task->{if(!main.post(task))throw new java.util.concurrent.RejectedExecutionException("UI encerrada");},
        value->{if(current(value.generation)){
            if(value.rich!=null)listener.richFrame(value.rich,value.arrived);
            else listener.frame(value.simple,value.arrived);
        }});
        heartbeat.scheduleAtFixedRate(()->{if(desiredAim&&process!=null)controlPump.request();},80,80,java.util.concurrent.TimeUnit.MILLISECONDS);
    }
    private boolean current(int id){return generation==id;}
    private void aimStatus(int id,String value){main.post(()->{if(current(id))listener.aimStatus(value);});}
    private void status(int id,String value,String log){main.post(()->{if(current(id))listener.status(value,lastAimDiagnostic.isEmpty()?log:(lastAimDiagnostic+"\n"+log));});}
    private void frame(int id,float[] points){if(current(id))frames.offer(new UiFrame(id,SystemClock.uptimeMillis(),points,null));}
    private static String quote(String value){return "'"+value.replace("'","'\\''")+"'";}
    static float[] parseFrame(String line){
        String[] fields=line.trim().split("\\s+");
        if(fields.length<2 || !fields[0].equals("FRAME"))throw new IllegalArgumentException("Frame sem cabeçalho");
        int count=Integer.parseInt(fields[1]);
        if(count<0 || count>128 || fields.length!=2+3*count)throw new IllegalArgumentException("Tamanho de frame inválido");
        float[] result=new float[count*3];
        for(int i=0;i<result.length;i++){float value=Float.parseFloat(fields[i+2]);if(!Float.isFinite(value) || (i%3<2?(value<0 || value>1):(value<0 || value>5000)))throw new IllegalArgumentException("Ponto inválido");result[i]=value;}
        return result;
    }
    public void start(){
        stop();lastAimDiagnostic="";final int id=generation;
        worker=new Thread(()->{
            while(current(id)){
                lastAimDiagnostic="";
                aimStatus(id,"Leitor desconectado; aguardando dados");
                StringBuilder log=new StringBuilder("ARROZ MENU / 3.1.7\n");Process launched=null;
                try{
                    if(!binary.isFile())throw new IllegalStateException("Executável ARM64 ausente");
                    status(id,"Conectando leitor com root…",log.toString());
                    launched=new ProcessBuilder("su","-c",quote(binary.getAbsolutePath())+" --stream-esp").redirectErrorStream(true).start();
                    if(!current(id)){launched.destroyForcibly();break;}process=launched;setFov(selectedFov);
                    try(BufferedReader input=new BufferedReader(new InputStreamReader(launched.getInputStream()))){
                        String line;while(current(id) && (line=input.readLine())!=null){
                            if(line.startsWith("FRAME2 ") || line.startsWith("FRAME3 ") || line.startsWith("FRAME4 ")){try{EspFrameCodec.Frame r=EspFrameCodec.parse(line);long arrived=SystemClock.uptimeMillis();if(current(id))frames.offer(new UiFrame(id,arrived,null,r));}catch(RuntimeException e){status(id,"Quadro rejeitado; aguardando leitura válida",log.toString());}}else if(line.startsWith("FRAME ")){
                                try{frame(id,parseFrame(line));}catch(RuntimeException error){status(id,"Dados inválidos; aguardando nova leitura",log.toString());}
                            }else{
                                if((line.startsWith("HS_DIAG ") || line.startsWith("AIM_DIAG ")) && current(id))lastAimDiagnostic=line;
                                log.append(line).append('\n');if(log.length()>10000)log.delete(0,log.length()-8000);
                                if(line.startsWith("STATUS "))status(id,line.substring(7),log.toString());
                                if(line.startsWith("AIMSTATUS "))aimStatus(id,line.substring(10));
                            }
                        }
                    }
                    if(current(id))status(id,"Leitor encerrou; reconectando em 5 segundos…",log.toString());
                }catch(Exception error){if(current(id))status(id,"Leitura: "+error.getMessage(),log.toString()+"\n"+error);}
                finally{if(process==launched)process=null;if(launched!=null)terminate(launched);frame(id,new float[0]);aimStatus(id,"Leitor desconectado; aguardando reconexão");}
                if(current(id))try{Thread.sleep(5000);}catch(InterruptedException ignored){break;}
            }
        },"rdzim-esp");worker.start();
    }
    public void stop(){
        desiredAim=false;controlPackets.disarm();
        ++generation;Thread oldWorker=worker;worker=null;if(oldWorker!=null)oldWorker.interrupt();
        aimStatus(generation,"Controles desligados");
        Process oldProcess=process;process=null;if(oldProcess!=null)terminate(oldProcess);
    }
    public void setTarget(int value){selectedTarget=Math.max(0,Math.min(2,value));sendControl();}
    public void setVisuals(int value){selectedVisuals=Math.max(0,Math.min(15,value));sendControl();}
    public void setFov(int value){selectedFov=Math.max(0,Math.min(360,value));sendControl();}
    private void sendControl(){controlPump.request();}
    public void setAimEnabled(boolean held){if(!held)controlPackets.disarm();desiredAim=held;controlPump.request();}
    private void writeLatestControl(){
        final int id=generation;Process target=process;if(target==null||!current(id))return;
        ReaderControlPackets.Packet packet=controlPackets.prepare(selectedTarget,selectedVisuals,selectedFov,desiredAim);
        if(target!=process||!current(id)){controlPackets.failed(packet);return;}
        try{target.getOutputStream().write(packet.text.getBytes(StandardCharsets.US_ASCII));target.getOutputStream().flush();}catch(Exception ignored){controlPackets.failed(packet);}
    }
    private static void terminate(Process target){ReaderShutdown.stop(target);}
    public void close(){heartbeat.shutdownNow();stop();controlPump.close();commands.shutdownNow();}
}
