/*
 * This class contain the information about a node.
 * Its IP address, battery level and alarm status.
 */
public class Node {
	public Node(String IP, int batt_value, int alarm_status) {
		ip=IP;
		batttery_value=batt_value;
		alarm=alarm_status;
	}
	public int batttery_value; // % of the remeaning battery
	public String ip; // IP of the node
	public int alarm; // alarm status for this node
}