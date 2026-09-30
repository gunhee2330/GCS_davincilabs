package org.mavlink.qgroundcontrol.voicerelay;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.InputType;
import android.util.TypedValue;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/** Starts the relay and shows where it forwards to and whether the payload answers. */
public final class MainActivity extends Activity {
    private static final String POST_NOTIFICATIONS = "android.permission.POST_NOTIFICATIONS";
    private static final long REFRESH_MSECS = 1000;

    private final Handler m_handler = new Handler(Looper.getMainLooper());
    private final Runnable m_refresh = new Runnable() {
        @Override
        public void run() {
            refresh();
            m_handler.postDelayed(this, REFRESH_MSECS);
        }
    };
    private TextView m_status;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Without it Android 13 hides the notification that shows the relay is running.
        if ((Build.VERSION.SDK_INT >= 33) &&
                (checkSelfPermission(POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)) {
            requestPermissions(new String[] {POST_NOTIFICATIONS}, 1);
        }
        RelayService.start(this);

        final int padding = dp(16);
        final LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(padding, padding, padding, padding);

        final TextView title = text("DAVINCI 음성중계", 22);
        column.addView(title);
        column.addView(text("GCS의 실시간 음성을 Tailscale로 기체 스피커에 넘깁니다. 마이크는 쓰지 않습니다.\n"
            + "이 앱은 Tailscale 분할 터널링 제외 목록에 넣지 마세요.\n"
            + "앱을 강제 종료하면 다시 열 때까지 부팅 시 자동 시작이 되지 않습니다.", 15));

        column.addView(text("기체 스피커 Tailscale 주소", 15));
        final EditText target = new EditText(this);
        target.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        target.setText(RelayService.target(this));
        column.addView(target);

        final Button save = new Button(this);
        save.setText("저장");
        save.setOnClickListener(view -> {
            final String address = target.getText().toString().trim();
            if (RelayService.parseTarget(address) == null) {
                Toast.makeText(this, "IPv4 주소를 넣으세요 (예: " + RelayService.DEFAULT_TARGET + ")",
                    Toast.LENGTH_LONG).show();
                return;
            }
            RelayService.setTarget(this, address);
            RelayService.start(this);
        });
        column.addView(save);

        m_status = text("", 15);
        column.addView(m_status);

        final ScrollView scroll = new ScrollView(this);
        scroll.addView(column);
        setContentView(scroll);
    }

    @Override
    protected void onResume() {
        super.onResume();
        m_handler.post(m_refresh);
    }

    @Override
    protected void onPause() {
        m_handler.removeCallbacks(m_refresh);
        super.onPause();
    }

    private void refresh() {
        final long lastReplyAt = RelayService.s_lastReplyAt;
        final String lastReply = (lastReplyAt == 0)
            ? "없음"
            : (((SystemClock.elapsedRealtime() - lastReplyAt) / 1000) + "초 전");
        final StringBuilder status = new StringBuilder()
            .append("\n중계: ").append(RelayService.s_running ? "동작 중" : "멈춤")
            .append("\nTailscale: ").append(RelayService.s_tailscaleUp ? "연결됨" : "꺼짐 (보내지 않고 버림)")
            .append("\n받는 곳: 127.0.0.1:").append(RelayService.COMMAND_PORT)
            .append(", ").append(RelayService.AUDIO_PORT)
            .append("\n보낸 패킷: ").append(RelayService.s_forwarded)
            .append(", 버린 패킷: ").append(RelayService.s_held)
            .append("\n스피커 응답: ").append(RelayService.s_replies).append("회, 마지막 ").append(lastReply);
        final String error = RelayService.s_lastError;
        if (!error.isEmpty()) {
            status.append("\n마지막 오류: ").append(error);
        }
        m_status.setText(status);
    }

    private TextView text(final String value, final int sp) {
        final TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setPadding(0, dp(6), 0, dp(6));
        return view;
    }

    private int dp(final int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
