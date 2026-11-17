package com.rdzim.memorycheck;
import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Bundle;
import android.provider.Settings;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import android.text.*;
import android.view.*;
import android.widget.*;
import java.util.concurrent.*;
public final class LicenseActivity extends Activity {
    private final ExecutorService network=Executors.newSingleThreadExecutor();
    private boolean busy,destroyed;
    private TextView status;
    private EditText key;
    private CheckBox remember;
    private Button enter;
    private SavedLicenseKey savedKey;
    private int dp(int n){return MenuLook.dp(this,n);}
    @Override public void onCreate(Bundle state){
        super.onCreate(state);if(LicenseAccess.allowed()){openMenu();return;}
        savedKey=new SavedLicenseKey(this);FrameLayout root=MenuLook.backdrop(this);ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);
        LinearLayout page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);page.setGravity(Gravity.CENTER);page.setPadding(dp(22),dp(24),dp(22),dp(24));scroll.addView(page);root.addView(scroll,new FrameLayout.LayoutParams(-1,-1));
        page.setOnApplyWindowInsetsListener((v,i)->{android.graphics.Insets b=i.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());v.setPadding(dp(22)+b.left,dp(24)+b.top,dp(22)+b.right,dp(24)+b.bottom);return i;});setContentView(root);page.requestApplyInsets();
        LinearLayout card=MenuLook.card(this);card.setPadding(dp(24),dp(28),dp(24),dp(26));page.addView(card,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout avatar=new LinearLayout(this);avatar.setGravity(Gravity.CENTER);avatar.addView(MenuLook.portrait(this,true),new LinearLayout.LayoutParams(dp(78),dp(78)));card.addView(avatar);
        TextView brand=MenuLook.text(this,"ARROZ MENU",25,MenuLook.INK);brand.setTypeface(null,Typeface.BOLD);brand.setGravity(Gravity.CENTER);brand.setLetterSpacing(.05f);brand.setPadding(0,dp(20),0,dp(8));card.addView(brand);
        TextView welcome=MenuLook.text(this,"Bem-vindo de volta",14,MenuLook.MUTED);welcome.setGravity(Gravity.CENTER);card.addView(welcome);
        key=new EditText(this);key.setSingleLine(true);key.setHint("Sua chave de acesso");key.setTextColor(MenuLook.INK);key.setHintTextColor(MenuLook.MUTED);key.setTextSize(15);key.setPadding(dp(16),0,dp(16),0);key.setBackground(MenuLook.rounded(this,0xff0e172b,18));key.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_PASSWORD|InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);key.setFilters(new InputFilter[]{new InputFilter.LengthFilter(256)});key.setSaveEnabled(false);key.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);key.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE);
        LinearLayout.LayoutParams field=new LinearLayout.LayoutParams(-1,dp(58));field.topMargin=dp(26);card.addView(key,field);
        remember=new CheckBox(this);remember.setText("Lembrar minha chave");remember.setTextColor(0xffc4cce4);remember.setTextSize(13);remember.setButtonTintList(MenuLook.thumb());remember.setMinHeight(dp(44));remember.setPadding(0,dp(4),0,0);card.addView(remember);remember.setOnCheckedChangeListener((b,on)->{if(!on)savedKey.clear();});
        enter=MenuLook.action(this,"Entrar",true);LinearLayout.LayoutParams action=new LinearLayout.LayoutParams(-1,dp(54));action.topMargin=dp(10);card.addView(enter,action);enter.setOnClickListener(v->login());key.setOnEditorActionListener((v,id,event)->{if(id==android.view.inputmethod.EditorInfo.IME_ACTION_DONE){if(enter.isEnabled())login();return true;}return false;});
        status=MenuLook.text(this,"Preparando acesso…",12,MenuLook.MUTED);status.setGravity(Gravity.CENTER);status.setPadding(0,dp(18),0,0);card.addView(status);status.setTextIsSelectable(true);
        TextView footer=MenuLook.text(this,"ARROZ MENU  ·  3.1.7",10,MenuLook.MUTED);footer.setGravity(Gravity.CENTER);footer.setLetterSpacing(.1f);footer.setPadding(0,dp(22),0,0);page.addView(footer);
        controls(false);
        network.execute(()->{String value="";boolean problem=false;try{value=savedKey.load();}catch(Exception e){problem=true;}final String restored=value;final boolean failed=problem;runOnUiThread(()->{if(destroyed)return;key.setText(restored);remember.setChecked(!restored.isEmpty());controls(true);status.setText(failed?"Digite sua chave novamente.":"Digite sua chave para continuar.");});});
    }
    private void controls(boolean enabled){enter.setEnabled(enabled);enter.setAlpha(enabled?1f:.55f);key.setEnabled(enabled);remember.setEnabled(enabled);}
    private void login(){
        if(busy||!enter.isEnabled())return;final String value=key.getText().toString().trim();final boolean keep=remember.isChecked();
        if(value.isEmpty()){key.setError("Digite sua chave");return;}
        busy=true;controls(false);status.setText("Validando chave…");
        network.execute(()->{
            try{
                String androidId=Settings.Secure.getString(getContentResolver(),Settings.Secure.ANDROID_ID);
                if(androidId==null||androidId.isEmpty())throw new ArrozKeyClient.AuthException("Identificação do aparelho indisponível.");
                byte[] digest=MessageDigest.getInstance("SHA-256").digest(("arroz-device-v1|"+getPackageName()+"|"+androidId).getBytes(StandardCharsets.UTF_8));
                StringBuilder device=new StringBuilder();
                for(byte b:digest)device.append(String.format(java.util.Locale.ROOT,"%02x",b&255));
                ArrozKeyClient client=ArrozKeyClient.withProgress(message->runOnUiThread(()->{if(!destroyed&&busy)status.setText(message);}));ArrozKeyClient.Grant grant=client.license(value,device.toString());
                boolean stored=true;try{if(keep)savedKey.save(value);else savedKey.clear();}catch(Exception e){stored=false;savedKey.clear();}final boolean remembered=stored;
                runOnUiThread(()->{if(destroyed)return;busy=false;LicenseAccess.grant(client,grant);key.setText("");if(keep&&!remembered)Toast.makeText(this,"Acesso validado; a chave não pôde ser lembrada neste aparelho.",Toast.LENGTH_LONG).show();openMenu();});
            }catch(Exception error){final String message=AuthFailure.describe(error);runOnUiThread(()->{if(destroyed)return;busy=false;controls(true);status.setText(message);});}
        });
    }
    private void openMenu(){startActivity(new Intent(this,MainActivity.class));finish();}
    @Override protected void onDestroy(){destroyed=true;network.shutdownNow();super.onDestroy();}
}
