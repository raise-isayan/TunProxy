package tun.utils;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import tun.proxy.R;

public abstract class ProgressTask<Params, Progress, Result> {
    private static final String TAG = "ProgressTask";

    private volatile Status mStatus = Status.PENDING;
    private boolean canceled = false;

    public final ProgressTask.Status getStatus() {
        return mStatus;
    }

    @SafeVarargs
    public final void execute(Params... params) {
        try (ExecutorService executorService = Executors.newSingleThreadExecutor()) {
            executorService.submit(new ProgressRunnable(params));
        }
    }

    protected void onPreExecute() {
    }

    @SuppressWarnings("unchecked")
    protected abstract Result doInBackground(Params... params);

    protected void onPostExecute(Result result) {
    }

    public void cancel(boolean flag) {
        canceled = flag;
    }

    public final boolean isCancelled() {
        return canceled;
    }

    protected void onCancelled() {
    }

    public enum Status {PENDING, RUNNING, FINISHED}

    private class ProgressRunnable implements Runnable {

        final Params[] params;
        Handler handler = new Handler(Looper.getMainLooper());
        private Result result;

        @SafeVarargs
        public ProgressRunnable(Params... params) {
            this.params = params;
        }

        @Override
        public void run() {
            if (mStatus != Status.PENDING) {
                switch (mStatus) {
                    case RUNNING:
                        throw new IllegalStateException("Cannot execute task:"
                                + " the task is already running.");
                    case FINISHED:
                        throw new IllegalStateException("Cannot execute task:"
                                + " the task has already been executed "
                                + "(a task can be executed only once)");
                }
            }
            mStatus = Status.RUNNING;
            try {
                onPreExecute();
                result = doInBackground(params);
            } catch (Exception ex) {
                Log.e(TAG, ex.getMessage(), ex);
            }
            handler.post(new Runnable() {
                @Override
                public void run() {
                    if (!canceled) {
                        onPostExecute(result);
                        mStatus = Status.FINISHED;
                    } else {
                        onCancelled();
                    }
                }
            });
        }
    }
}