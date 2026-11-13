package io.github.pellcorp.grumpyscreen;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executor;

/** Checks printer retries and delayed network replies without Android or a printer. */
public final class PrinterConnectionCheckTest {
    public static void main(String[] args) {
        selectionUsesCachedStatus();
        offlinePrintersRetryIndependently();
        refreshInvalidatesOldReplies();
        discoverySupersedesPendingChecks();
        selectionIgnoresOldReplies();
        forgettingAndClosingStopChecks();
        manualAddressRetries();
        System.out.println("Printer connection and offline retry tests passed");
    }

    private static void selectionUsesCachedStatus() {
        Fixture f = new Fixture();
        f.online.add("printer:7125");
        f.check.watch("printer", 7125);
        f.check.watch("printer", 80);
        require(!f.check.canConnect(), "Connect starts disabled");
        f.check.refresh();
        f.check.select("printer", 7125);
        require(!f.check.canConnect(), "Connect stays disabled while checking");
        require(f.states.get("printer:7125") == PrinterConnectionCheck.State.CHECKING, "Shows checking");
        f.background.run(0);
        require(!f.check.canConnect(), "Waits for the reply on the UI thread");
        f.main.run(0);
        require(f.check.canConnect(), "A reachable selection enables Connect");
        f.finishChecks();
        f.check.select("printer", 80);
        require(!f.check.canConnect(), "An offline selection disables Connect");
        require(f.states.get("printer:80") == PrinterConnectionCheck.State.OFFLINE, "Shows offline");
        f.check.clearSelection();
        f.check.select("printer", 7125);
        require(f.check.canConnect(), "Selecting a known online printer enables Connect");
        require(f.background.pending.isEmpty(), "Selection changes do not recheck listed printers");
        require(f.requests.size() == 2, "Each host and port is checked independently");
    }

    private static void offlinePrintersRetryIndependently() {
        Fixture f = new Fixture();
        f.online.add("selected:7125");
        f.check.watch("selected", 7125);
        f.check.watch("second", 7125);
        f.check.watch("third", 7126);
        f.check.refresh();
        f.check.select("selected", 7125);
        f.finishChecks();
        require(f.check.canConnect(), "The selected printer is online");
        require(f.check.hasOffline(), "Unselected offline printers still need retries");

        f.check.clearSelection();
        f.check.select("second", 7125);
        f.online.add("second:7125");
        f.check.retryOffline();
        require(f.background.pending.size() == 2, "Retries all offline printers across selection changes");
        require(f.states.get("selected:7125") == PrinterConnectionCheck.State.ONLINE,
                "Retries leave the online printer alone");
        f.check.retryOffline();
        require(f.background.pending.size() == 2, "Does not overlap pending checks");
        f.finishChecks();
        require(f.check.canConnect(), "The selected printer becomes connectable when it appears");
        require(f.check.hasOffline(), "Another offline printer keeps retrying");

        f.check.select("selected", 7125);
        f.online.add("third:7126");
        f.check.retryOffline();
        require(f.background.pending.size() == 1, "Only the remaining offline printer is retried");
        f.finishChecks();
        require(f.states.get("third:7126") == PrinterConnectionCheck.State.ONLINE,
                "An unselected printer updates when it appears");
        require(!f.check.hasOffline(), "Retries stop after all printers appear");
        f.check.retryOffline();
        f.check.select("second", 7125);
        require(f.background.pending.isEmpty(), "Online printers stay unchecked until Search again");
        require(f.requests.equals(List.of("selected:7125", "second:7125", "third:7126",
                "second:7125", "third:7126", "third:7126")), "Only offline addresses are retried");
    }

    private static void refreshInvalidatesOldReplies() {
        Fixture f = new Fixture();
        f.online.add("printer:7125");
        f.check.watch("printer", 7125);
        f.check.refresh();
        f.check.select("printer", 7125);
        f.finishChecks();
        require(f.check.canConnect(), "Initial check succeeds");

        f.check.refresh();
        require(!f.check.canConnect(), "Search again immediately invalidates the online result");
        f.background.run(0);
        f.online.clear();
        f.check.refresh();
        f.background.run(0);
        f.main.run(1);
        f.main.run(0);
        require(!f.check.canConnect(), "An older success cannot overwrite a newer offline result");
        require(f.states.get("printer:7125") == PrinterConnectionCheck.State.OFFLINE,
                "Search again detects a printer that went offline");
        require(f.check.hasOffline(), "A refreshed offline printer needs retries");
    }

    private static void discoverySupersedesPendingChecks() {
        Fixture f = new Fixture();
        f.check.watch("printer", 7125);
        f.check.refresh();
        f.check.select("printer", 7125);
        f.background.run(0);
        f.check.found("printer", 7125);
        require(f.check.canConnect(), "Network discovery confirms the printer is online");
        f.main.run(0);
        require(f.check.canConnect(), "An old failed probe cannot undo network discovery");
        f.check.retryOffline();
        require(f.background.pending.isEmpty(), "Discovery stops retries for that printer");

        f.check.found("other", 7126);
        f.check.select("other", 7126);
        require(f.check.canConnect(), "Discovered printers can be selected without another probe");
        f.check.refresh();
        require(f.background.pending.size() == 2, "Search again rechecks saved and discovered printers");
    }

    private static void selectionIgnoresOldReplies() {
        Fixture f = new Fixture();
        f.online.add("online:7125");
        f.check.select("online", 7125);
        f.background.run(0);
        f.check.clearSelection();
        f.check.select("offline", 7126);
        f.background.run(0);
        f.main.run(1);
        f.main.run(0);
        require(!f.check.canConnect(), "A delayed reply cannot enable another selection");
        require(f.states.get("offline:7126") == PrinterConnectionCheck.State.OFFLINE,
                "Keeps the current selection's status");

        f.check.clearSelection();
        f.check.retryOffline();
        require(f.background.pending.isEmpty(), "Editing stops retries for the old typed address");
    }

    private static void forgettingAndClosingStopChecks() {
        Fixture f = new Fixture();
        f.check.watch("forgotten", 7125);
        f.check.refresh();
        f.background.run(0);
        f.check.remove("forgotten", 7125);
        f.main.run(0);
        f.check.retryOffline();
        require(!f.check.hasOffline() && f.background.pending.isEmpty(),
                "A forgotten printer's pending failure does not restart retries");

        f.online.add("printer:7125");
        f.check.select("printer", 7125);
        f.background.run(0);
        f.check.cancel();
        require(!f.check.canConnect(), "Closing clears the selection");
        f.online.clear();
        f.check.select("printer", 7125);
        f.background.run(0);
        f.main.run(1);
        f.main.run(0);
        require(!f.check.canConnect(), "A closed picker's reply cannot enable a reopened selection");
        require(f.states.get("printer:7125") == PrinterConnectionCheck.State.OFFLINE,
                "Keeps the reopened picker's current result");
        f.check.cancel();
        f.check.refresh();
        f.check.retryOffline();
        require(f.background.pending.isEmpty(), "Closing discards every watched printer");
    }

    private static void manualAddressRetries() {
        Fixture f = new Fixture();
        f.check.select("manual", 7125);
        f.finishChecks();
        require(!f.check.canConnect() && f.check.hasOffline(), "An offline typed address needs retries");
        f.online.add("manual:7125");
        f.check.retryOffline();
        f.finishChecks();
        require(f.check.canConnect() && !f.check.hasOffline(), "A typed address becomes connectable");
        f.check.retryOffline();
        require(f.background.pending.isEmpty(), "A successful typed address stops retrying");
    }

    private static final class Fixture {
        final QueueExecutor background = new QueueExecutor();
        final QueueExecutor main = new QueueExecutor();
        final List<String> requests = new ArrayList<>();
        final Set<String> online = new HashSet<>();
        final Map<String, PrinterConnectionCheck.State> states = new HashMap<>();
        final PrinterConnectionCheck check = new PrinterConnectionCheck(background, main, (host, port) -> {
            String address = host + ":" + port;
            requests.add(address);
            return online.contains(address);
        }, (host, port, state) -> states.put(host + ":" + port, state));

        void finishChecks() {
            while (!background.pending.isEmpty()) {
                background.run(0);
            }
            while (!main.pending.isEmpty()) {
                main.run(0);
            }
        }
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
