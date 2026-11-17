package com.rdzim.memorycheck;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.content.res.Configuration;
import android.graphics.Insets;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.IBinder;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.graphics.Color;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowInsets;
import android.view.WindowMetrics;
import android.view.WindowManager;

public class OverlayService extends Service implements SharedPreferences.OnSharedPreferenceChangeListener {
    private final Handler licenseHandler=new Handler(Looper.getMainLooper());
    private final java.util.concurrent.ExecutorService licenseNetwork=java.util.concurrent.Executors.newSingleThreadExecutor();
    private boolean licenseChecking,serviceClosed;
    private long nextLicenseCheck;
    private final Runnable licensePulse=new Runnable(){public void run(){
        if(serviceClosed)return;
        if(!LicenseAccess.allowed()){stopForLicense();return;}
        long now=android.os.SystemClock.elapsedRealtime();
        if(!licenseChecking&&now>=nextLicenseCheck){
            licenseChecking=true;nextLicenseCheck=now+45000;
            licenseNetwork.execute(()->{
                boolean valid=true;try{LicenseAccess.check();}catch(Exception error){valid=false;}
                final boolean checked=valid;licenseHandler.post(()->{if(serviceClosed)return;licenseChecking=false;if(!checked)stopForLicense();});
            });
        }
        licenseHandler.postDelayed(this,1000);
    }};
    private void stopForLicense(){prefs.edit().putString("error","Acesso encerrado. Abra o Arroz Menu para validar a chave.").apply();stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}
    public static volatile boolean active=false;
    private WindowManager manager;
    private CrosshairView crosshair;
    private WindowManager.LayoutParams crosshairParams;
    private EspLinesView espLines;
    private EspSession espSession;
        private boolean aiming;
    private FloatingMenu menu;
    private WindowManager.LayoutParams menuParams;
    private boolean collapsed;
    private SharedPreferences prefs;
    private static final String CHANNEL="crosshair_status";
    @Override public void onCreate() {
        super.onCreate();
        prefs=getSharedPreferences("crosshair",MODE_PRIVATE);
        if(prefs.getInt("ui_revision",0)<28)prefs.edit().putInt("ui_revision",28).putInt("menu_size",0).putBoolean("menu_collapsed",true).apply();
        prefs.edit().putBoolean("esp_enabled",false).putBoolean("aim_enabled",false).apply();
        manager=getSystemService(WindowManager.class);
        espSession=new EspSession(this,new EspSession.Listener(){
            public void richFrame(EspFrameCodec.Frame frame,long arrived){if(espLines!=null)espLines.updateRich(frame,arrived);}
            public void frame(float[] points,long arrived){if(espLines!=null)espLines.update(points,arrived);}
            public void status(String value,String log){prefs.edit().putString("esp_status",value).putString("esp_log",log).apply();}
            public void aimStatus(String value){prefs.edit().putString("aim_status",value).apply();}
        });
        NotificationChannel channel=new NotificationChannel(CHANNEL,"Menu flutuante",NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Controle para fechar o menu e a mira");
        getSystemService(NotificationManager.class).createNotificationChannel(channel);
    }
    @Override public int onStartCommand(Intent intent,int flags,int startId) {
        if(intent!=null && "STOP".equals(intent.getAction())) {
            stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();return START_NOT_STICKY;
        }
        if(!LicenseAccess.allowed()){stopForLicense();return START_NOT_STICKY;}
        if(!Settings.canDrawOverlays(this)) {
            prefs.edit().putString("error","Autorize a exibição sobre outros apps.").apply();
            stopSelf();return START_NOT_STICKY;
        }
        try {
            Intent home=new Intent(this,MainActivity.class);
            PendingIntent open=PendingIntent.getActivity(this,0,home,PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
            Intent off=new Intent(this,OverlayService.class).setAction("STOP");
            PendingIntent stop=PendingIntent.getService(this,1,off,PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
            Notification notification=new Notification.Builder(this,CHANNEL)
                    .setContentTitle("Arroz Menu ativo")
                    .setContentText("Toque para ajustar ou use Desligar para fechar tudo.")
                    .setSmallIcon(android.R.drawable.ic_menu_view)
                    .setContentIntent(open).setOngoing(true)
                    .addAction(android.R.drawable.ic_menu_close_clear_cancel,"Desligar",stop)
                    .setCategory(Notification.CATEGORY_SERVICE).build();
            if(Build.VERSION.SDK_INT>=34) startForeground(15,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            else startForeground(15,notification);
            if(crosshair==null) {
                crosshair=new CrosshairView(this);
                int pixels=Math.round(96*getResources().getDisplayMetrics().density);
                WindowManager.LayoutParams params=new WindowManager.LayoutParams(
                        pixels,pixels,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE|
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN|WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                        PixelFormat.TRANSLUCENT);
                params.gravity=Gravity.CENTER;
                params.alpha=0.65f;
                if(Build.VERSION.SDK_INT>=31) {
                    InputManager input=getSystemService(InputManager.class);
                    params.alpha=Math.min(0.65f,Math.max(0f,input.getMaximumObscuringOpacityForTouch()*0.9f));
                }
                params.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                crosshairParams=params;
                manager.addView(crosshair,params);
                prefs.registerOnSharedPreferenceChangeListener(this);
                crosshair.setVisibility(prefs.getBoolean("enabled",true)?View.VISIBLE:View.GONE);
                showMenu();
                prefs.edit().remove("error").apply();
                active=true;
                nextLicenseCheck=android.os.SystemClock.elapsedRealtime()+45000;
                licenseHandler.post(licensePulse);
            } else if(menu!=null) setCollapsed(false);
        } catch(Exception error) {
            prefs.edit().putString("error","Falha ao abrir menu: "+error.getMessage()).apply();
            stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();
        }
        return START_NOT_STICKY;
    }
    @Override public void onSharedPreferenceChanged(SharedPreferences preferences,String key) {
        if("esp_log".equals(key))return;
        if("esp_status".equals(key)||"aim_status".equals(key)){if(menu!=null)menu.refreshStatus();return;}
        if(crosshair!=null){crosshair.setVisibility(prefs.getBoolean("enabled",true)?View.VISIBLE:View.GONE);crosshair.invalidate();}
        if(menu!=null)menu.refresh();
        if("menu_size".equals(key) && menu!=null && menuParams!=null && menu.isAttachedToWindow()){clampMenu();manager.updateViewLayout(menu,menuParams);savePosition();}
        if(espLines!=null)espLines.invalidate();
        if(("esp_box".equals(key)||"esp_skeleton".equals(key)||"esp_name".equals(key)||"esp_health".equals(key)||"esp_enabled".equals(key)) && espSession!=null)espSession.setVisuals(visualMask());
        if("aim_target".equals(key) && espSession!=null)espSession.setTarget(prefs.getInt("aim_target",0));
        if("aim_fov".equals(key) && espSession!=null)espSession.setFov(prefs.getInt("aim_fov",130));
        if("esp_enabled".equals(key) || "aim_enabled".equals(key)){updateEsp();updateAimActivation();}
    }
    private int visualMask(){if(!prefs.getBoolean("esp_enabled",false))return 0;return (prefs.getBoolean("esp_box",true)?1:0)|(prefs.getBoolean("esp_skeleton",false)?2:0)|(prefs.getBoolean("esp_name",true)?4:0)|(prefs.getBoolean("esp_health",false)?8:0);}
    private void armAim(boolean enabled){
        aiming=enabled && prefs.getBoolean("aim_enabled",false);
        if(espSession!=null)espSession.setAimEnabled(aiming);
    }
    private void updateAimActivation(){armAim(prefs.getBoolean("aim_enabled",false));}
    private float overlayBudget(){
        float threshold=0.8f;
        if(Build.VERSION.SDK_INT>=31)threshold=getSystemService(InputManager.class).getMaximumObscuringOpacityForTouch();
        return Math.max(0f,Math.min(1f,threshold*0.9f));
    }
    private void updateEsp(){
        if(prefs.getBoolean("esp_enabled",false) || prefs.getBoolean("aim_enabled",false)){
            if(espLines!=null)return;
            try{
                espLines=new EspLinesView(this);
                WindowManager.LayoutParams params=new WindowManager.LayoutParams(-1,-1,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE|
                    WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN|WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,PixelFormat.TRANSLUCENT);
                params.gravity=Gravity.TOP|Gravity.LEFT;params.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                // Two non-touchable overlay windows share the maximum obscuring-opacity budget.
                float alpha=Math.min(0.45f,1f-(float)Math.sqrt(1f-overlayBudget()));params.alpha=alpha;
                if(crosshair!=null && crosshairParams!=null){crosshairParams.alpha=alpha;manager.updateViewLayout(crosshair,crosshairParams);}
                manager.addView(espLines,params);updateExcludedMenu();prefs.edit().putString("esp_status","Conectando leitor com root…").apply();espSession.setTarget(prefs.getInt("aim_target",0));espSession.setFov(prefs.getInt("aim_fov",130));espSession.setVisuals(visualMask());espSession.start();
            }catch(Exception error){prefs.edit().putString("esp_status","Falha na sobreposição: "+error.getMessage()).putBoolean("esp_enabled",false).apply();}
        }else{
            espSession.stop();
            if(espLines!=null){try{manager.removeView(espLines);}catch(Exception ignored){}espLines=null;}
            if(crosshair!=null && crosshairParams!=null){crosshairParams.alpha=Math.min(0.65f,overlayBudget());manager.updateViewLayout(crosshair,crosshairParams);}
        }
    }
    private void updateExcludedMenu(){if(espLines!=null && menuParams!=null)espLines.excludeMenu(menuParams.x,menuParams.y,menuParams.width,menuParams.height);}
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private Rect usableBounds(){
        WindowMetrics metrics=manager.getCurrentWindowMetrics();Rect rect=new Rect(metrics.getBounds());
        Insets bars=metrics.getWindowInsets().getInsetsIgnoringVisibility(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
        // TOP/LEFT window coordinates are relative to the display origin.
        return new Rect(bars.left,bars.top,Math.max(bars.left+1,rect.width()-bars.right),Math.max(bars.top+1,rect.height()-bars.bottom));
    }
    private void showMenu(){
        menu=new FloatingMenu(this,prefs,new FloatingMenu.Actions(){
            public void collapse(boolean value){setCollapsed(value);}
            public void close(){stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}
        });
        menuParams=new WindowManager.LayoutParams(dp(MenuLook.width(prefs)),dp(MenuLook.height(prefs)),WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL|
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,PixelFormat.TRANSLUCENT);
        menuParams.gravity=Gravity.TOP|Gravity.LEFT;
        menuParams.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;
        menuParams.x=prefs.getInt("menu_x",dp(12));menuParams.y=prefs.getInt("menu_y",dp(64));
        collapsed=prefs.getBoolean("menu_collapsed",true);menu.setCollapsed(collapsed);clampMenu();
        attachDrag(menu.dragHandle(),false);attachDrag(menu.bubbleHandle(),true);
        manager.addView(menu,menuParams);
    }
    private void clampMenu(){
        if(menuParams==null)return;
        Rect bounds=usableBounds();
        menuParams.width=Math.min(dp(collapsed?44:MenuLook.width(prefs)),bounds.width());
        menuParams.height=Math.min(dp(collapsed?44:MenuLook.height(prefs)),bounds.height());
        menuParams.x=Math.max(bounds.left,Math.min(menuParams.x,bounds.right-menuParams.width));
        menuParams.y=Math.max(bounds.top,Math.min(menuParams.y,bounds.bottom-menuParams.height));
        updateExcludedMenu();
    }
    private void savePosition(){prefs.edit().putInt("menu_x",menuParams.x).putInt("menu_y",menuParams.y).putBoolean("menu_collapsed",collapsed).apply();}
    private void setCollapsed(boolean value){
        if(menu==null || menuParams==null)return;
        collapsed=value;menu.setCollapsed(value);clampMenu();manager.updateViewLayout(menu,menuParams);savePosition();
    }
    private void attachDrag(View handle,boolean clickable){
        handle.setOnTouchListener(new View.OnTouchListener(){
            float downX,downY;int initialX,initialY;boolean dragged;
            final int slop=ViewConfiguration.get(OverlayService.this).getScaledTouchSlop();
            @Override public boolean onTouch(View view,MotionEvent event){
                if(menuParams==null)return false;
                switch(event.getActionMasked()){
                    case MotionEvent.ACTION_DOWN:
                        downX=event.getRawX();downY=event.getRawY();initialX=menuParams.x;initialY=menuParams.y;dragged=false;return true;
                    case MotionEvent.ACTION_MOVE:
                        float dx=event.getRawX()-downX,dy=event.getRawY()-downY;
                        if(!dragged && dx*dx+dy*dy>slop*slop)dragged=true;
                        if(dragged){menuParams.x=initialX+Math.round(dx);menuParams.y=initialY+Math.round(dy);clampMenu();manager.updateViewLayout(menu,menuParams);}return true;
                    case MotionEvent.ACTION_UP:
                        if(dragged)savePosition();else if(clickable)view.performClick();return true;
                    case MotionEvent.ACTION_CANCEL:
                        if(dragged)savePosition();return true;
                    default:return true;
                }
            }
        });
    }
    @Override public void onConfigurationChanged(Configuration config){
        super.onConfigurationChanged(config);
        updateAimActivation();
        if(espLines!=null)espLines.clear();
        if(menu!=null && menuParams!=null){clampMenu();manager.updateViewLayout(menu,menuParams);savePosition();}
    }
    @Override public void onDestroy() {
        serviceClosed=true;licenseHandler.removeCallbacksAndMessages(null);licenseNetwork.shutdownNow();
        active=false;
        if(prefs!=null)prefs.unregisterOnSharedPreferenceChangeListener(this);
        armAim(false);
        if(espSession!=null)espSession.close();
        if(prefs!=null)prefs.edit().putBoolean("esp_enabled",false).putBoolean("aim_enabled",false).apply();
        if(espLines!=null){try{manager.removeView(espLines);}catch(Exception ignored){}espLines=null;}
        if(menu!=null){try{manager.removeView(menu);}catch(Exception ignored){}menu=null;menuParams=null;}
        if(crosshair!=null) {
            try {manager.removeView(crosshair);}catch(Exception ignored){}
            crosshair=null;
        }
        super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) {return null;}
}
