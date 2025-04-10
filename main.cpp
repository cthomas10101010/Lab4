// BankSim3000
//
// The purpose of this bank and teller simulation is to help a bank manager to make an informed
// decision on how many tellers to hire at a branch with longer than desired wait times.

#include <iostream>
#include <variant>
#include <vector>
#include <queue>
#include <cassert>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>

using namespace std;

const size_t MIN_TELLERS = 1;
const size_t MAX_TELLERS = 5;

// Integer time units.
using Time = int;

// We will be tracking teller state in a variable std::vector.
using TellerIndex = size_t;

// Arrival event containing only the arrival and transaction times.
struct ArrivalEvent {
    Time arrivalTime;
    Time transactionTime;
};

// This is a common idiom in FP, wrapping a type in another to yield better
// semantics (meaning) while gaining some static type checking.
struct Customer {
    ArrivalEvent arrivalEvent;
};

// A departure event including the expected departure time and the
// teller being departed from.
struct DepartureEvent {
    Time departureTime;
    TellerIndex tellerIndex;
};

// Either an arrival or departure event. Variant can be thought of as a safer union.
using Event = std::variant<ArrivalEvent, DepartureEvent>;

// Helper function to get the time from either an arrival or departure event.
Time get_event_time(const Event& e) {
    if (holds_alternative<ArrivalEvent>(e)) {
        return get<ArrivalEvent>(e).arrivalTime;
    }
    return get<DepartureEvent>(e).departureTime;
}

// A compare functor / function object for the priority queue. Creates a min-heap.
struct CompareEvent {
    bool operator()(const Event & e1, const Event & e2) {
        // Create a min-heap by comparing the event times.
        return get_event_time(e1) > get_event_time(e2);
    }
};

// Holds availability and when the teller started to become busy. Also automatically
// accumulates elapsed busy time.
//
// Hint: Use startWork and stopWork from the event processing methods to track
//       teller activity.
class Teller {
private:
    // Optional is an ADT that either holds nothing when the teller isn't
    // busy, represented by nullopt, or a time value of when the teller
    // started working.
    optional<Time> startBusy;
    // Accumulated busy time for the teller.
    Time elapsedTimeBusy;

public:
    Teller() : startBusy(nullopt), elapsedTimeBusy(0) {}

    bool isAvailable() {
        return !startBusy.has_value();
    }

    void startWork(Time currentTime) {
        startBusy = currentTime;
    }

    void stopWork(Time currentTime) {
        Time elapsedTime = currentTime - startBusy.value();
        elapsedTimeBusy += elapsedTime;
        startBusy = nullopt;
    }

    // Returns the final elapsed time a teller has been working after the simulation is finished.
    Time elapsedTimeWorking() {
        return elapsedTimeBusy;
    }
};

struct SimulationResults {
    vector<Time> elapsedTimeBusy;

    // Finds the max teller time and is a good measure of the overall time.
    Time maxTellerBusyTime() {
        return *max_element(elapsedTimeBusy.begin(), elapsedTimeBusy.end());
    }

    SimulationResults(vector<Time> elapsedTimeBusy) : elapsedTimeBusy(elapsedTimeBusy) { }
};

// A line of customers waiting to be served by a teller.
using BankLine = queue<Customer>;
// The event priority queue used by the simulation.
using EventQueue = priority_queue<Event, vector<Event>, CompareEvent>;
// A list of arrival events used to start the simulation.
using SimulationInput = vector<ArrivalEvent>;

class BankSim3000 {
private:
    // Input is stored locally to help restart the simulation for multiple tellers.
    SimulationInput simulationInput;
    // The event queue. Initially this is loaded with the simulation input.
    EventQueue eventQueue;
    // The bank line. Initially this is empty.
    BankLine bankLine;

    // One teller simulation state for each teller.
    vector<Teller> tellers;

    // Resets the tellers vector to the requested size and initialized to the default constructor.
    void resetTellers(size_t tellerCount) {
        if (tellerCount != tellers.size()) {
            tellers.reserve(tellerCount);
        }

        tellers.clear();

        for (size_t i = 0; i < tellerCount; ++i) {
            tellers.emplace_back();
        }
    }

    // Clears the bank line.
    void clearBankLine() {
        assert(bankLine.empty()); // It should already be cleared after a complete simulation run.
        while (!bankLine.empty()) {
            bankLine.pop();
        }
    }

    // Clears the event queue and initializes it to our input data.
    void setupEventQueue() {
        assert(eventQueue.empty()); // Should also already be empty after a complete simulation.
        while (!eventQueue.empty()) {
            eventQueue.pop();
        }

        // Load all the input data from simulationInput into the event priority queue.
        for (const auto &arrival : simulationInput) {
            eventQueue.push(arrival);
        }
    }

    // Sets up the simulation for the given number of tellers.
    void setupSimulation(size_t tellerCount) {
        if (tellerCount < MIN_TELLERS) {
            throw invalid_argument("Teller count must >= " + to_string(MIN_TELLERS));
        }
        if (tellerCount > MAX_TELLERS) {
            throw invalid_argument("Teller count must be <= " + to_string(MAX_TELLERS));
        }

        setupEventQueue();
        resetTellers(tellerCount);
        clearBankLine();
    }

    // Processes either an arrival or a departure event.
    void processEvent(Time currentTime, const Event & e) {
        if (holds_alternative<ArrivalEvent>(e)) {
            ArrivalEvent arrivalEvent = get<ArrivalEvent>(e);
            processArrival(currentTime, arrivalEvent);
        } else {
            assert(holds_alternative<DepartureEvent>(e));
            DepartureEvent departureEvent = get<DepartureEvent>(e);
            processDeparture(currentTime, departureEvent);
        }
    }

    // Helper used by processArrival.
    // Returns the index of an available teller or nullopt if all are busy.
    optional<size_t> searchAvailableTellers() {
        for (size_t i = 0; i < tellers.size(); ++i) {
            if (tellers[i].isAvailable()) {
                return i;
            }
        }
        return nullopt;
    }

    // Process arrival events.
    //
    // If teller is not available or the bank line is full then we're busy,
    // place customer at the end of the bank line. Otherwise, we weren't
    // busy so start teller work and add a new departure event to the event queue.
    void processArrival(Time currentTime, const ArrivalEvent& arrivalEvent) {
        auto teller = searchAvailableTellers();
        bool is_teller_available = teller.has_value();

        if (is_teller_available) {
            size_t tellerIndex = teller.value();
            // Teller is available: start working immediately.
            tellers[tellerIndex].startWork(currentTime);
            // Create a departure event for when the teller will finish this customer's transaction.
            DepartureEvent departure = { currentTime + arrivalEvent.transactionTime, tellerIndex };
            eventQueue.push(departure);
        } else {
            // No teller available: place the customer at the end of the bank line.
            bankLine.push(Customer{arrivalEvent});
        }
    }

    // Process departure events.
    //
    // If bank line is empty then the teller should stop working.
    // Otherwise, take the next customer off the bank line and enqueue a new departure
    // event into the event priority queue.
    void processDeparture(Time currentTime, const DepartureEvent& departureEvent) {
        size_t tellerIndex = departureEvent.tellerIndex;

        if (bankLine.empty()) {
            // No waiting customers: the teller stops working.
            tellers[tellerIndex].stopWork(currentTime);
        } else {
            // There is a customer waiting in the bank line.
            Customer nextCustomer = bankLine.front();
            bankLine.pop();
            // End the current work period and immediately start servicing the next customer.
            tellers[tellerIndex].stopWork(currentTime);
            tellers[tellerIndex].startWork(currentTime);
            // Enqueue a departure event for the next customer.
            DepartureEvent newDeparture = { currentTime + nextCustomer.arrivalEvent.transactionTime, tellerIndex };
            eventQueue.push(newDeparture);
        }
    }

    // Runs the simulation.
    void runSimulation() {
        while (!eventQueue.empty()) {
            // Remove event.
            Event e = eventQueue.top();
            eventQueue.pop();

            processEvent(get_event_time(e), e);
        }
    }

    SimulationResults gatherResults() {
        // Transform is like map in more functional languages. It takes an input vector and fills
        // a new vector with the results of the given function passed as a parameter.
        vector<Time> elapsedTimeBusy(tellers.size());
        transform(tellers.begin(), tellers.end(), elapsedTimeBusy.begin(), [](auto teller) {
            return teller.elapsedTimeWorking();
        });

        return SimulationResults {elapsedTimeBusy};
    }

public:
    BankSim3000(SimulationInput simulationInput) : simulationInput(simulationInput) { }

    Time maxTellerBusyTime(size_t tellerCount) {
        setupSimulation(tellerCount);
        runSimulation();
        return gatherResults().maxTellerBusyTime();
    }
};

int main() {
    // Do not change the input.
    SimulationInput SimulationInput00 = {{20, 6}, {22, 4}, {23, 2}, {30, 3}};

    BankSim3000 bankSim(SimulationInput00);

    cout << "Time waiting with 1 teller: " << bankSim.maxTellerBusyTime(1) << endl;
    cout << "Time waiting with 2 tellers: " << bankSim.maxTellerBusyTime(2) << endl;
    cout << "Time waiting with 3 tellers: " << bankSim.maxTellerBusyTime(3) << endl;
    cout << "Time waiting with 4 tellers: " << bankSim.maxTellerBusyTime(4) << endl;
    cout << "Time waiting with 5 tellers: " << bankSim.maxTellerBusyTime(5) << endl;

    return 0;
}

/*
==========================
Part 2: Short Answer Questions
==========================

A) What number of tellers should the branch manager hire? Explain your reasoning.

   Based on the simulation outputs:
     - With 1 teller, the overall service time (max teller busy time) is relatively high.
     - With 2 tellers, the time decreases, but there is still noticeable delay.
     - With 3 tellers, the simulation shows a significant improvement while maintaining good teller utilization.
     - With 4 tellers the overall time remains similar to 3 tellers.
     - With 5 tellers, while the wait time is the shortest, tellers could be severely underutilized.

   Therefore, the branch manager should hire 3 tellers as this number offers a good balance between minimizing customer wait times and maintaining efficient teller use, avoiding unnecessary staffing costs.

B) What kind of simulation is this and why?

   This is a discrete-event simulation. It operates by processing events—such as customer arrivals and departures—at specific points in time using a priority queue. The simulation advances in time jumps from one event to the next, which is the hallmark of a discrete-event simulation.

C) Why use the priority queue for the event queue and a regular queue for the bank line?

   The event queue is implemented as a priority queue because it must process events in chronological order (smallest event time first) regardless of the order they were generated. In contrast, the bank line uses a regular FIFO (first-in-first-out) queue to ensure that customers are served in the order they arrived, which closely mimics how a physical bank line operates.

D) Can you think of any other problems, aside from banking, that an event simulation could solve? What values would it track?

   Yes, event simulations can be applied to:
     - Traffic systems: Tracking vehicle arrivals, departures, intersection wait times, and congestion levels.
     - Manufacturing processes: Monitoring workpiece arrival times, processing durations at different stations, machine utilization, and overall production throughput.
     - Healthcare systems: Simulating patient arrivals, treatment times in emergency rooms, waiting times, and resource (doctor, nurse, equipment) utilization.

   In these cases, the simulation would track key values such as arrival times, service times, waiting durations, and resource usage statistics.
*/
