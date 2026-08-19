
package org.happytimesoft.util;

import java.io.File;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.net.wifi.WifiManager.MulticastLock;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.Build;
import android.os.StrictMode;
import androidx.core.content.FileProvider;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import android.content.ActivityNotFoundException;


public class HtUtil
{
    private static MulticastLock    m_wifiLock;
    private static WakeLock         m_wakeLock;

    public HtUtil()
    {
    }

    public static void enableMulticast(Context context)
    {
        WifiManager wm = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
        m_wifiLock = wm.createMulticastLock("htutil");

        if (m_wifiLock != null)
        {
            m_wifiLock.setReferenceCounted(true);
            m_wifiLock.acquire();
        }
    }

    public static void disableMulticast()
    {
        if (m_wifiLock != null)
        {
            m_wifiLock.release();
            m_wifiLock = null;
        }    
    }

    private static String getMIMEType(String filename)
    {
        if (filename.endsWith(".jpg") ||
            filename.endsWith(".jpeg") ||
            filename.endsWith(".JPG") ||
            filename.endsWith(".JPEG"))
            return "image/jpeg";
        if (filename.endsWith(".mp4") || filename.endsWith(".MP4"))
            return "video/mp4";
        if (filename.endsWith(".avi") || filename.endsWith(".AVI"))
            return "video/x-msvideo";
        if (filename.endsWith(".txt") || filename.endsWith(".TXT"))
            return "text/plain";
        else
            return "*/*";
    }

    public static void openFile(Context context, String path, String provider)
    {
        Uri     uri;
        Intent  intent = new Intent(Intent.ACTION_VIEW);
        File    file = new File(path);
        String  type = getMIMEType(path);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N)
        {
            uri = FileProvider.getUriForFile(context, provider, file);
        }
        else
        {
            uri = Uri.fromFile(file);
        }

        try {
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            intent.setDataAndType(uri, type);
            intent.addCategory(Intent.CATEGORY_DEFAULT);

            context.startActivity(intent);
        } 
        catch (ActivityNotFoundException e)
        {
        }
    }

    public static void disableLockScreen(Context context)
    {
        PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        m_wakeLock = pm.newWakeLock(pm.SCREEN_BRIGHT_WAKE_LOCK | pm.ON_AFTER_RELEASE, "htutil");
        
        if (m_wakeLock != null)
        {
            m_wakeLock.acquire();
        }
    }

    public static void enableLockScreen()
    {
        if (null != m_wakeLock && m_wakeLock.isHeld())
        {
            m_wakeLock.release();
            m_wakeLock = null;
        }
    }

    public static int requestPermission(Context context, String permission)
    {
        int r = ContextCompat.checkSelfPermission(context, permission);
        if (r != PackageManager.PERMISSION_GRANTED)
        {
            ActivityCompat.requestPermissions((Activity)context, new String[]{permission}, 10000);

            r = ContextCompat.checkSelfPermission(context, permission);
            if (r != PackageManager.PERMISSION_GRANTED)
            {
                return 0;
            }
            else
            {
                return 1;
            }
        }
        else
        {
            return 1;
        }
    }
}


