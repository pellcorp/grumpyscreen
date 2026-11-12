package io.github.pellcorp.grumpyscreen;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicReference;

/** Checks selection changes and delayed network replies without Android or a printer. */
public final class PrinterConnectionCheckTest {
    public static void main(String[] args) {
        QueueExecutor background = new QueueExecutor();
        QueueExecutor main = new QueueExecutor();
        List<String> requests = new ArrayList<>();
        AtomicReference<PrinterConnectionCheck.State> displayed = new AtomicReference<>();
        PrinterConnectionCheck check = new PrinterConnectionCheck(background, main, (host, port) -> {
            requests.add(host + ":" + port);
            return host.equals("online");
        }, displayed::set);

        require(!check.canConnect(), "Connect starts disabled");
        check.check("online", 7125);
        require(!check.canConnect(), "Connect stays disabled while checking");
        require(displayed.get() == PrinterConnectionCheck.State.CHECKING, "Shows checking");
        background.run(0);
        require(!check.canConnect(), "Waits for the reply on the UI thread");
        main.run(0);
        require(check.canConnect(), "A reachable selection enables Connect");
        require(displayed.get() == PrinterConnectionCheck.State.ONLINE, "Shows online before connecting");
        require(requests.size() == 1 && requests.get(0).equals("online:7125"),
                "Only probes the selected address");

        check.check("offline", 80);
        require(!check.canConnect(), "Changing the selection immediately disables Connect");
        background.run(0);
        main.run(0);
        require(!check.canConnect(), "An offline printer cannot connect");
        require(displayed.get() == PrinterConnectionCheck.State.OFFLINE, "Shows offline");

        // An old successful reply must not enable the newly selected offline printer.
        check.check("online", 7125);
        background.run(0);
        check.check("offline", 7126);
        background.run(0);
        main.run(1);
        main.run(0);
        require(!check.canConnect(), "A delayed reply cannot enable another selection");
        require(displayed.get() == PrinterConnectionCheck.State.OFFLINE, "Keeps the current status");

        // Editing the address, closing the picker, or backgrounding cancels old replies.
        check.check("online", 7125);
        background.run(0);
        check.cancel();
        main.run(0);
        require(!check.canConnect(), "Cancelling invalidates an outstanding successful reply");
        require(displayed.get() == PrinterConnectionCheck.State.UNCHECKED, "Clears the old status");

        check.check("online", 7125);
        background.run(0);
        main.run(0);
        require(check.canConnect(), "A fresh check works after reopening");
        check.check("offline", 7125);
        require(!check.canConnect(), "Refreshing immediately clears the previous online result");
        background.run(0);
        main.run(0);
        require(!check.canConnect(), "A printer that goes offline loses Connect");

        System.out.println("Printer connection selection tests passed");
    }

    private static final class QueueExecutor implements Executor {
        private final List<Runnable> pending = new ArrayList<>();

        @Override
        public void execute(Runnable task) {
            pending.add(task);
        }

        void run(int index) {
            pending.remove(index).run();
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }
}
