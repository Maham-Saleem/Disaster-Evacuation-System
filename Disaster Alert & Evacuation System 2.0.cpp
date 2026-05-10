#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <limits>
#include <sstream>
#include <curl/curl.h>
#include <json/json.h>
using namespace std;
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}
string fetchHttp(const string& url, const vector<string>& headers = {}) {
    CURL* curl = curl_easy_init(); //creates CURL handle
    string buffer; //variable to store API response
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        // these tell curl where to send request, what function to call when data comes and where to store the data
        struct curl_slist* headerList = nullptr;
        for (size_t i = 0; i < headers.size(); i++) {
            headerList = curl_slist_append(headerList, headers[i].c_str());   // stores all headers in link list form
        }
        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);  //attach headers to url to give instructions to server how to treat the request
        curl_easy_perform(curl);  //sends request
        curl_easy_cleanup(curl);  //clears space used by CURL
    }
    return buffer;
}
string geocodePlace(const string& place, const string& apiKey) {
    string encoded;
    for(int i = 0; i < place.length(); i++) {
        if (place[i] == ' ')
            encoded += "%20";
        else
            encoded += place[i];
    } // replaces spaces with %20 for url generation
    string url =
        "https://api.opencagedata.com/geocode/v1/json?q=" +
        encoded + "&key=" + apiKey + "&limit=1";  
    string response = fetchHttp(url); 
    Json::Value root; //Json variable to store parsed data
    Json::CharReaderBuilder builder;  //creates parser 
    string errs; //stores errors
    istringstream s(response); // converts URL response into stream
    if (!Json::parseFromStream(builder, s, &root, &errs))
        return "";
    if (root["results"].empty())
        return "";

    string lat = root["results"][0]["geometry"]["lat"].asString();
    string lon = root["results"][0]["geometry"]["lng"].asString();

    return lon + "," + lat;   // ORS expects lon,lat
}
struct RouteInfo {
    int duration;                 // seconds
    vector<string> instructions; // turn-by-turn
    RouteInfo() {
        duration = -1;
    }
};
RouteInfo getRouteORS(const string& start,const string& end,const string& apiKey) 
{
    RouteInfo route;
    route.duration = -1;
    string url =
        "https://api.openrouteservice.org/v2/directions/driving-car?start=" +
        start + "&end=" + end;
    vector<string> headers = {
     "Authorization: " + apiKey,
    };
    string response = fetchHttp(url, headers); 
    //set json parsing
    Json::Value root;
    Json::CharReaderBuilder builder;
    string errs;
    istringstream s(response);
    if (!Json::parseFromStream(builder, s, &root, &errs))
        return route;
    if (!root.isMember("features") || root["features"].empty())
        return route;
    Json::Value summary = root["features"][0]["properties"]["summary"]; //extract summary (stores route duration and distance)
    route.duration = summary["duration"].asInt();
    Json::Value steps = root["features"][0]["properties"]["segments"][0]["steps"];  //extract route duration steps
    for (Json::Value::ArrayIndex i = 0; i < steps.size(); i++) {
        string instr = steps[i]["instruction"].asString();
        route.instructions.push_back(instr);
    } 
    return route;
}
struct Person {
    string name;
    string type;      // priority / normal
    string location;  // lon,lat
};
class Graph {
    vector<vector<int>> adj;
    int n;
    const int INF = (numeric_limits<int>::max)();
public:
    Graph(int size) {
        n = size;
        adj.assign(n, vector<int>(n, INF));
        for (int i = 0; i < n; i++) adj[i][i] = 0;
    }
    void updateEdge(int u, int v, int w) {
        adj[u][v] = w;
        adj[v][u] = w;
    }
    vector<int> dijkstra(int src) {
        vector<int> dist(n, INF);
        vector<bool> visited(n, false);
        dist[src] = 0;
        for (int i = 0; i < n - 1; i++) {
            int u = -1, minVal = INF;
            for (int j = 0; j < n; j++)
                if (!visited[j] && dist[j] < minVal) {
                    minVal = dist[j];
                    u = j;
                }
            if (u == -1) break;
            visited[u] = true;
            for (int v = 0; v < n; v++)
                if (!visited[v] && adj[u][v] != INF &&
                    dist[u] + adj[u][v] < dist[v])
                    dist[v] = dist[u] + adj[u][v];
        }
        return dist;
    }
};
void addSafeLocation(vector<string>& safeLocations, const string& geoKey) {
    string place;
    cout << "Enter safe location name: ";
    getline(cin, place);
    string coord = geocodePlace(place, geoKey);
    if (coord == "") {
        cout << "Geocoding failed.\n";
        return;
    }
    safeLocations.push_back(coord);
    cout << "Safe location added.\n";
}
void addPerson(vector<Person>& people, const string& geoKey) {
    Person p;
    cout << "Enter name: ";
    getline(cin, p.name);
    string place;
    cout << "Enter current location: ";
    getline(cin, place);
    p.location = geocodePlace(place, geoKey);
    if (p.location == "") {
        cout << "Invalid location.\n";
        return;
    }
    cout << "Type (priority/normal): ";
    getline(cin, p.type);
    // Insert person in priority order
    int insertIndex = people.size(); 
    for (int i = 0; i < people.size(); i++) {
        if (p.type == "priority" && people[i].type != "priority") {
            insertIndex = i;
            break;
        }
    }
    people.insert(people.begin() + insertIndex, p); // insert at correct position
    cout << "Person added.\n";
}
Person getNextPerson(vector<Person>& people) {
    if (people.empty()) {
        cout << "No people to evacuate!\n";
        return Person();  // return an empty/default person
    }
    Person p = people.front();  // first person in queue
    people.erase(people.begin());
    return p;
}
void evacuate(vector<Person>& people, vector<string>& safeLocations, const string& orsKey) {
    if (people.empty() || safeLocations.empty()) {
        cout << "Nothing to evacuate or no safe locations.\n";
        return;
    }
    Person p = getNextPerson(people);  
    vector<string> nodes;
    nodes.push_back(p.location);
    nodes.insert(nodes.end(), safeLocations.begin(), safeLocations.end());
    Graph g(nodes.size());
    for (int i = 0; i < nodes.size(); i++) {
        for (int j = i + 1; j < nodes.size(); j++) {
            RouteInfo r = getRouteORS(nodes[i], nodes[j], orsKey);
            if (r.duration > 0)
                g.updateEdge(i, j, r.duration);
        }
    }
    vector<int> dist = g.dijkstra(0);
    int best = -1, minTime = (numeric_limits<int>::max)();
    for (int i = 1; i < dist.size(); i++) {
        if (dist[i] < minTime) {
            minTime = dist[i];
            best = i;
        }
    }
    if (best == -1) {
        cout << "No reachable safe location.\n";
        return;
    }
    RouteInfo finalRoute = getRouteORS(p.location, nodes[best], orsKey);
    cout << "\nEvacuation Plan\n";
    cout << "Person: " << p.name << " (" << p.type << ")\n";
    cout << "Destination coordinates: " << nodes[best] << "\n";
    cout << "\nRoute Instructions:\n";
    for (int i = 0; i < finalRoute.instructions.size(); i++) {
        cout << i + 1 << ". " << finalRoute.instructions[i] << "\n";
    }
    cout << "\nEstimated Time: " << finalRoute.duration / 60 << " minutes\n";
}
void showSafeLocations(const vector<string>& safeLocations) {
    if (safeLocations.empty()) {
        cout << "No safe locations added yet.\n";
        return;
    }
    cout << "\n=== Safe Locations ===\n";
    int i = 0;
    while (i < safeLocations.size()) {
        cout << i + 1 << ". " << safeLocations[i] << "\n";
        i++;
    }
}
void showPeopleQueue(const vector<Person>& people) {
    if (people.empty()) {
        cout << "Evacuation queue is empty.\n";
        return;
    }
    cout << "\n=== People in Evacuation Queue ===\n";
    int count = 1;
    int i = 0;
    while (i < people.size()) {
        cout << count++ << ". Name: " << people[i].name
            << " | Type: " << people[i].type
            << " | Location: " << people[i].location << "\n";
        i++;
    }
}
int main() {
    const string OPENCAGE_KEY = "c0f9cf0a35774bdfaaf198f2bb8ef95f";
    const string ORS_KEY = "eyJvcmciOiI1YjNjZTM1OTc4NTExMTAwMDFjZjYyNDgiLCJpZCI6ImM4MjA5MDM1ZjcxMzQ0MjNiZThhNTgxOTI4MmRkODBhIiwiaCI6Im11cm11cjY0In0=";
    vector<string> safeLocations;
    vector<Person> evacQueue;
    while (true) {
        cout << "\n=== EVACUATION SYSTEM ===\n";
        cout << "1. Add Safe Location\n";
        cout << "2. Add Person\n";
        cout << "3. Evacuate Next Person\n";
        cout << "4. Display Safe Locations\n";
        cout << "5. Display People Queue\n";
        cout << "6. Exit\n";
        cout << "Choice: ";
        int choice;
        cin >> choice;
        cin.ignore();
        switch (choice) {
        case 1:
            addSafeLocation(safeLocations, OPENCAGE_KEY);
            break;
        case 2:
            addPerson(evacQueue, OPENCAGE_KEY);
            break;
        case 3:
            evacuate(evacQueue, safeLocations, ORS_KEY);
            break;
        case 4:
            showSafeLocations(safeLocations);
            break;
        case 5:
            showPeopleQueue(evacQueue);
            break;
        case 6:
            cout << "System shutting down.\n";
            return 0;
        default:
            cout << "Invalid choice.\n";
        }
    }
}


