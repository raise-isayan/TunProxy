package tun.utils;

import android.os.Handler;
import android.os.Looper;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public abstract class ProgressTask<Params, Progress, Result> {
    private volatile Status mStatus = Status.PENDING;
    private boolean canceled = false;

    public final ProgressTask.Status getStatus() {
        return  mStatus;
    }

    private class ProgressRunnable implements Runnable {

        final Params [] params;

        @SafeVarargs
        public ProgressRunnable(Params... params) {
            this.params = params;
        }

        private Result result;
        Handler handler = new Handler(Looper.getMainLooper());

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
                ex.printStackTrace();
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

    @SafeVarargs
    public final void execute(Params... params) {
        try (ExecutorService executorService  = Executors.newSingleThreadExecutor()) {
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

    public enum Status { PENDING, RUNNING, FINISHED }
}