package com.rdzim.memorycheck;
import android.Manifest;
import android.app.Activity;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.*;
import android.provider.Settings;
import android.view.*;
import android.widget.*;

public final class MainActivity extends Activity {
    private final Handler main=new Handler(Looper.getMainLooper());
    private TextView status;
    private android.content.SharedPreferences prefs;
    private int dp(int n){return MenuLook.dp(this,n);}
    private boolean access(){if(LicenseAccess.allowed())return true;startActivity(new Intent(this,LicenseActivity.class));finish();return false;}
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);if(!access())return;prefs=getSharedPreferences("crosshair",MODE_PRIVATE);
        FrameLayout root=MenuLook.backdrop(this);ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);
        LinearLayout page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);page.setGravity(Gravity.CENTER);page.setPadding(dp(22),dp(24),dp(22),dp(24));scroll.addView(page);root.addView(scroll,new FrameLayout.LayoutParams(-1,-1));
        page.setOnApplyWindowInsetsListener((v,i)->{android.graphics.Insets b=i.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());v.setPadding(dp(22)+b.left,dp(24)+b.top,dp(22)+b.right,dp(24)+b.bottom);return i;});setContentView(root);page.requestApplyInsets();
        LinearLayout card=MenuLook.card(this);page.addView(card,new LinearLayout.LayoutParams(-1,-2));
        FrameLayout hero=new FrameLayout(this);hero.addView(MenuLook.cover(this),new FrameLayout.LayoutParams(-1,-1));View shade=new View(this);shade.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,new int[]{0x10121b32,0x70121b32,0xff19213d}));hero.addView(shade,new FrameLayout.LayoutParams(-1,-1));card.addView(hero,new LinearLayout.LayoutParams(-1,dp(196)));
        TextView badge=MenuLook.text(this,"●  CONECTADO",10,0xff8ff5d6);badge.setLetterSpacing(.12f);badge.setPadding(dp(12),dp(7),dp(12),dp(7));badge.setBackground(MenuLook.rounded(this,0xb50c2a31,16));FrameLayout.LayoutParams badgeLp=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.LEFT);badgeLp.setMargins(dp(20),dp(18),0,0);hero.addView(badge,badgeLp);
        LinearLayout brand=new LinearLayout(this);brand.setOrientation(LinearLayout.VERTICAL);brand.setPadding(dp(22),0,dp(22),dp(18));TextView title=MenuLook.text(this,"ARROZ MENU",28,MenuLook.INK);title.setTypeface(null,Typeface.BOLD);title.setLetterSpacing(.04f);brand.addView(title);TextView subtitle=MenuLook.text(this,"Seu painel. Seu estilo.",13,0xffc6ceeb);subtitle.setPadding(0,dp(5),0,0);brand.addView(subtitle);hero.addView(brand,new FrameLayout.LayoutParams(-1,-2,Gravity.BOTTOM));
        LinearLayout actions=new LinearLayout(this);actions.setOrientation(LinearLayout.VERTICAL);actions.setPadding(dp(22),dp(10),dp(22),dp(24));card.addView(actions);
        Button floating=MenuLook.action(this,"Abrir flutuante",true),game=MenuLook.action(this,"Abrir jogo",false);actions.addView(floating,new LinearLayout.LayoutParams(-1,dp(58)));LinearLayout.LayoutParams gameLp=new LinearLayout.LayoutParams(-1,dp(58));gameLp.topMargin=dp(12);actions.addView(game,gameLp);
        status=MenuLook.text(this,"",12,MenuLook.MUTED);status.setGravity(Gravity.CENTER);status.setPadding(0,dp(18),0,0);actions.addView(status);
        TextView footer=MenuLook.text(this,"ARROZ MENU  ·  3.1.7",10,MenuLook.MUTED);footer.setLetterSpacing(.1f);footer.setPadding(0,dp(22),0,0);page.addView(footer);
        floating.setOnClickListener(v->openFloating());game.setOnClickListener(v->openGame());refreshStatus();
    }
    private void openFloating(){
        if(!access())return;
        if(!Settings.canDrawOverlays(this)){try{startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,Uri.parse("package:"+getPackageName())));status.setText("Autorize a sobreposição e volte para abrir.");}catch(ActivityNotFoundException e){status.setText("Autorize a sobreposição nas configurações do aplicativo.");}return;}
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED&&!prefs.getBoolean("notificationAsked",false)){prefs.edit().putBoolean("notificationAsked",true).apply();requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},55);return;}
        startFloating();
    }
    private void startFloating(){if(!access())return;try{prefs.edit().remove("error").apply();startForegroundService(new Intent(this,OverlayService.class));status.setText("Abrindo flutuante…");main.postDelayed(this::refreshStatus,500);}catch(RuntimeException e){status.setText("Não foi possível abrir o flutuante. Tente novamente.");}}
    private void openGame(){
        if(!access())return;Intent game=getPackageManager().getLaunchIntentForPackage("com.dts.freefireth");
        if(game==null){status.setText("Free Fire não encontrado neste aparelho.");return;}
        try{startActivity(game);}catch(ActivityNotFoundException|SecurityException e){status.setText("Não foi possível abrir o Free Fire.");}
    }
    private void refreshStatus(){if(status!=null)status.setText(OverlayService.active?"Flutuante ativo — toque na foto para abrir.":"Tudo pronto para começar.");}
    @Override protected void onResume(){super.onResume();if(access())refreshStatus();}
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){super.onRequestPermissionsResult(request,permissions,grants);if(request==55&&Settings.canDrawOverlays(this))startFloating();}
    @Override protected void onDestroy(){main.removeCallbacksAndMessages(null);super.onDestroy();}
}
