import java.io.IOException;
import java.io.PrintWriter;

import javax.servlet.ServletException;
import javax.servlet.annotation.WebServlet;
import javax.servlet.http.HttpServlet;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Timer;
import java.util.Vector;

import java.net.URI;

import org.eclipse.californium.core.CoapClient;
import org.eclipse.californium.core.CoapResponse;
import org.eclipse.californium.core.CoapObserveRelation;
import org.eclipse.californium.core.CoapHandler;

import java.util.TimerTask;
import java.lang.Integer;
import java.net.InetAddress;


/**
 * Servlet implementation class Servlet.
 * Realize the web interface for the user and allow to control the alarm in the LoWPan.
 */
@WebServlet("/Servlet")
public class Servlet extends HttpServlet {
    private static final long serialVersionUID = 1L;
    
    private final int MAXCOAPSERVERS=5; // number of node excluding border router
    private final int HTTPRefreshInterval=10; // seconds
    private final int criticalBatteryLevel=10; // percentage
    private final int batteryRefreshInterval=10000; // milli seconds
    private final String alarmURI=new String("coap://[aaaa::c30c:0:0:2]:5683/alarm"); // guardian node alarm resource
    
    private Timer timerGet; // timer used to schedule periodic CoAP get message to monitor the battery level
    private Vector<Node> list; // list of the node in the 6LoWPan
    private boolean alarm_tripped; // alarm flag: 1 if the guardian node notify the alarm, 0 otherwise
    private boolean alarm_activate; // alarm flag: 1 if alarm is enable, 0 if is disable
    private CoapClient borderRouter; // used to enable/disable the alarm in the 6LoWPan
    private Vector<CoapClient> coapRes; // list of battery resources of the nodes 
    private CoapObserveRelation alarmObserver; // used to observe the alarm resource on the guardian node
    
    class GetTask extends TimerTask { // the GetTask is called periodically by timer timerGet
    	/*
    	 * Update the status of the batteries and eventually set the alarm status and send a message to the user via telegram bot.
    	 */
        public void run() {
            int value;
            CoapResponse response;
            for (int i=0; i< MAXCOAPSERVERS; i++) { // for each node
            	if (list.elementAt(i).batttery_value==0 || list.elementAt(i).alarm==1) // if the node is dead or unreachable
            		continue ; // skip this node
                response = coapRes.elementAt(i).get(); // get the new value of the battery for this node
                if (response!=null) { // if a response is returned
                    value=Integer.parseInt(response.getResponseText());
                    System.out.println("Response from node "+list.elementAt(i).ip+", value: "+value); // debug info
                    if (value==0) { // if depleted battery
                    	list.elementAt(i).alarm=1; // the node is dead
                        alarm_tripped=true; // set tha alarm flag
                        try {
                        	if (alarm_activate)
                        		send_telegram_message("Depleted battery at node: "+list.elementAt(i).ip); // node dead due to depleted battery
                        } catch (Exception e) {
                            System.err.println("ERROR SENDING MESSAGE TO TELEGRAM WHILE MONITORING BATTERIES, BATTERY VALUE IS 0!");
                            e.printStackTrace();
                        }
                    }
                    else
                    	if (value < criticalBatteryLevel && list.elementAt(i).batttery_value >= criticalBatteryLevel) // if battery become critical
                    		try {
                    			if (alarm_activate)
                    				send_telegram_message("Low battery at node: "+list.elementAt(i).ip+", "+value+"% remaining!");
                    		} catch (Exception e) {
                    			System.err.println("ERROR SENDING MESSAGE TO TELEGRAM WHILE MONITORING BATTERIES, BATTERY VALUE BECAME CRITICAL!");
                    			e.printStackTrace();
                    		}
                    list.elementAt(i).batttery_value=value; // update the value
                }
                else { // no response from this node
                    System.out.println("--- NO RESPONSE FROM NODE "+list.elementAt(i).ip+" ---"); // debug info
                }
            }
        }
    }
    
    class AlarmHandler implements CoapHandler {
    	/*
    	 * Called when the guardian node notify the alarm.
    	 * Update the info about the alarm using the message received in the response parameter.
    	 */
        @Override
        public void onLoad(CoapResponse response) {
            Node dead;
            char msgType;
            String value=response.getResponseText();
            System.out.println("Allarme scattato: " + value + ", len: " + value.length()); // debug info
            if (value.length()!=0 && value.charAt(0)!='0') {
            	msgType=value.charAt(0); // type can be: unreachable node or IR sensor tripped
                value=value.substring(2);
                dead=findNodeByIP(value);
                if (dead==null) { // error, the dead node is not in the list
                    System.out.println("ERROR UNKNOWN DEAD NODE -> "+value); // debug info
                    return ;
                }
                
                if ((dead.alarm==1 && msgType=='1') || (dead.alarm==2 && msgType=='2')) { // multiple reporting! Do not send another telegram message
                    System.out.println("Allarme scartato"); // debug info
                    return ;
                }
                System.out.println("Allarme ok!"); // debug info
                alarm_tripped=true;
                
                if (msgType=='1') { // node unreachable
                	dead.alarm=1;
                	try {
                		if (alarm_activate)
                			send_telegram_message("Disappeared node: "+dead.ip); // KAG tripped, integrity no longer guaranteed!
                	} catch (Exception e) {
                		System.err.println("ERROR SENDING MESSAGE TO TELEGRAM, A DEAD NODE!");
                		e.printStackTrace();
                	}
                }
                if (msgType=='2') { // IR sensor tripped 
                	dead.alarm=2;
                    try {
                    	if (alarm_activate)
                    		send_telegram_message("Intruders reported at node: "+dead.ip); // IR sensor of this node is tripped
                    } catch (Exception e) {
                        System.err.println("ERROR SENDING MESSAGE TO TELEGRAM, INTRUDERS REPORTED!");
                        e.printStackTrace();
                    }
                }
            }
        }
        @Override
        public void onError() { // an error is occurred
            System.err.println("Failed alarm handler!");
        }
    }
    
    /*
     * Search in the list the node that have strIP as IP.
     * 
     * @parameter strIP, the IP we are looking for
     * 
     * @return the node wanted, or null if the srtIP does not exist in the list.
     */
    private Node findNodeByIP(String strIP) {
        try {
            for (int i=0; i<list.size(); i++)
                if (InetAddress.getByName(list.elementAt(i).ip).equals(InetAddress.getByName("aaaa"+strIP.substring(4))))
                    return list.elementAt(i);
        } catch (Throwable e) {
            System.err.println("ERROR comparing IP address!");
        }
        return null;
    }
    
    public Servlet() {
        super();
    }
    
    /*
     * Initialize variables, start the periodic timer to monitor the battery level and start the observation of the alarm resource
     * on the guardina node.
     */
    public void init() throws ServletException
    {
    	// inizialize variables
        alarm_tripped=false;
        alarm_activate=false;
        list=new Vector<Node>();
        coapRes=new Vector<CoapClient>();
        borderRouter=new CoapClient(alarmURI);
        
        URI uri = null;
        for (int i=0; i< MAXCOAPSERVERS; i++) { // for each node
            try {
                uri = new URI("coap://[aaaa::c30c:0:0:"+(i+2)+"]:5683/batttery");
            } catch (Exception e) {
                System.err.println("Error on new URI(coap://[aaaa::c30c:0:0:"+(i+2)+"]:5683/batttery): " + e.getMessage());
            }
            coapRes.add(new CoapClient(uri)); // creates a new CoapClient object
            list.add(new Node("aaaa::c30c:0:0:"+(i+2), 100, 0)); // add the node to the list
        }
        
        System.out.println("Start monitoring alarm and batteries...");
        timerGet=new Timer();
        timerGet.schedule(new GetTask(), 0, batteryRefreshInterval); // start periodic timer
        alarmObserver = (new CoapClient(alarmURI)).observe(new AlarmHandler()); // subscribe to the alarm resource
    }
    /**
     * Create the web interface for the user, this page contain the status of all the node with their battery level and a button
     * to enable/disable the alarm.
     */
    protected void doGet(HttpServletRequest request, HttpServletResponse response) throws ServletException, IOException {
        String colorStart, colorStop, status;
        response.setContentType("text/html;charset=UTF-8");
        //response.setHeader("Refresh", ""+HTTPRefreshInterval);
        try {
            PrintWriter out = response.getWriter();
            out.println("<head><meta http-equiv='refresh' content='"+HTTPRefreshInterval+"'></head>");
            if (alarm_tripped && alarm_activate)
                out.println("<font size='72' color='red'>ALARM TRIPPED!</font>");
            out.println("<TABLE border='1'>");
            out.println("<tr><td colspan='3' >List of node and percetage of battery</td></tr>");
            out.println("<tr><th> IP NODE </th>" +"<th> BATTERY </th>" +"<th> STATUS  </th></tr>");
            for (int i=0; i<list.size(); i++) {
                if (alarm_activate) { 
                    if (list.elementAt(i).alarm!=0) {
                    	if (list.elementAt(i).alarm==1) { // unreachable node
                    		colorStart="<font color='red'>";
                    		colorStop="</font>";
                    		status="DEAD";
                    	}
                    	else { // IR tripped, alarm==2
                            colorStart="<font color='magenta'>";
                            colorStop="</font>";
                            status="Intruders";
                        }
                    }
                    else {
                        if (list.elementAt(i).batttery_value < criticalBatteryLevel) { // critical battery level
                            colorStart="<font color='blue'>";
                            colorStop="</font>";
                            status="Low batt.";
                        }
                        else { // node is working fine
                            colorStart="";
                            colorStop="";
                            status="Active";
                        }
                    }
                }
                else { // alarm not active, no info about node status but battery level
                    colorStart="";
                    colorStop="";
                    status="---";
                }
                out.println("<tr><td><p align='center'>" + colorStart + list.elementAt(i).ip + colorStop + "</p></td><td><p align='center'>" + colorStart + list.elementAt(i).batttery_value + colorStop + "</p></td><td><p align='center'>" + colorStart + status + colorStop + "</p></td></tr>");
            }
            out.println("</TABLE>");
            out.println("<form method=\"POST\" enctype=\"multipart/form-data\" action=\"Servlet\">");
            if (alarm_activate)
                out.println("<input type=\"submit\" value=\"Disactivate\" name=\"buttonA/D\">");
            else
                out.println("<input type=\"submit\" value=\"Activate\" name=\"buttonA/D\">");
            out.println("</form>");
            out.println("<form method=\"GET\" enctype=\"multipart/form-data\" action=\"Servlet\">");
            out.println("<input type=\"submit\" value=\"refresh\" name=\"refresh\"></form>");
        } catch (Exception ex) {
            throw new ServletException(ex);
        }
    }
    
    /**
     * Send a CoAP put message to the guardian node to aneble/disable
     */
    protected void doPost(HttpServletRequest request, HttpServletResponse response) throws ServletException, IOException {
        alarm_activate=!alarm_activate;
        if (alarm_activate) { // enable alarm
            borderRouter.put("1", 0); // no doc about the second parameter
        }
        else { // disable alarm
            borderRouter.put("0", 0); // no doc about the second parameter
            alarm_tripped=false; // reset alarm flag
            for (int i=0; i<list.size(); i++) { // reset alarm status for each node
            	list.elementAt(i).alarm=0;
            }
        }
        doGet(request, response); // refresh the page
    }
    
    /*
     * Send a message to the user by telegram bot.
     * 
     * @paramenter message, the message we want to send.
     */
    public void send_telegram_message(String message)  throws Exception
    {
        String url = "https://api.telegram.org/bot168955001:AAHD0Lb1WY8Pj8zXKY0Z7zmExb-nXQChsHE/sendmessage?chat_id=61021181&text="+message;
        URL telegram_url = new URL(url);
        HttpURLConnection con = (HttpURLConnection) telegram_url.openConnection();
        // optional default is GET
        con.setRequestMethod("GET");
        //add request header
        con.setRequestProperty("User-Agent","Mozilla/5.0");
        con.getResponseCode();
    }
}
