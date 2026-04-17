import azure.functions as func
import logging
import pyodbc
import os
import json
from datetime import datetime, timedelta

app = func.FunctionApp(http_auth_level=func.AuthLevel.ANONYMOUS)

@app.route(route="log_alarm")
def log_alarm(req: func.HttpRequest) -> func.HttpResponse:
    logging.info('Python HTTP trigger function processed a request.')
    print("\n" + "="*50)
    print("---> [HTTP POST RECEIVED] Waking up function...")

    try:
        req_body = req.get_json()
        print(f"---> [PAYLOAD] Raw JSON received: {json.dumps(req_body)}")
        
        action_type = req_body.get('ActionType')
        reaction_time = req_body.get('ReactionTime_ms')

        # 1. Database Connection Phase
        server = os.environ["SQL_SERVER"] 
        database = os.environ["SQL_DATABASE"]
        username = os.environ["SQL_USER"]
        password = os.environ["SQL_PASSWORD"]
        driver = '{ODBC Driver 18 for SQL Server}'

        connection_string = f'DRIVER={driver};SERVER={server};PORT=1433;DATABASE={database};UID={username};PWD={password}'
        conn = pyodbc.connect(connection_string)
        cursor = conn.cursor()

        consecutive_snoozes = 0
        
        # 2. Insert Phase: Log today's button press
        insert_query = """
        INSERT INTO AlarmLogs (ActionType, ReactionTime_ms)
        VALUES (?, ?)
        """
        print(f"---> [SQL] Executing Insert: Action={action_type}, Time={reaction_time}ms")
        cursor.execute(insert_query, action_type, reaction_time)
        conn.commit()
        
        # 3. Logic Phase: ONLY calculate the average if it's a SNOOZE event
        if action_type == "SNOOZE":
            print("---> [MATH] Calculating 7-day moving average...")
            seven_days_ago = datetime.utcnow() - timedelta(days=7)
            
            # Query the average of all SNOOZE events from the last 7 days
            select_query = """
            SELECT AVG(CAST(ReactionTime_ms AS FLOAT))
            FROM AlarmLogs
            WHERE ActionType = 'SNOOZE' AND LogDate >= ?
            """
            # Pass just the date portion to match the LogDate column
            cursor.execute(select_query, seven_days_ago.date()) 
            row = cursor.fetchone()
            
            # If there's no data yet, default to 5000ms (5 seconds)
            avg_7_day = int(row[0]) if row[0] is not None else 5000
            print(f"---> [MATH] 7-Day Average calculated: {avg_7_day}ms")


            cursor.execute("""
            SELECT TOP 3 ActionType 
            FROM AlarmLogs 
            ORDER BY LogDate DESC, LogTime DESC
            """)

            recent_actions = [row.ActionType for row in cursor.fetchall()]
            if len(recent_actions) == 3 and all(action == 'SNOOZE' for action in recent_actions):
                consecutive_snoozes = 3
                logging.info("---> [SQL] Max snoozes detected in database history!")

            # 4. The dynamic snooze Algorithm
            base_snooze_ms = 240000       # 4 minutes (4 * 60 * 1000)
            penalty_snooze_ms = 60000    # 1 minutes (1 * 60 * 1000)
            punishment_snooze_ms = 1  # no more snooze, alarm instantly set off
            
            # 1. First, check if they have abused the snooze button (snooze more than 3 times)
            if consecutive_snoozes >= 3:
                new_snooze_ms = punishment_snooze_ms
                print("---> [LOGIC] Max snoozes reached! Punishment applied: 10 sec snooze.")   

            # 2. If under the limit, check their historical reaction time
            elif avg_7_day <= 10000:
                # Quick reaction (10 seconds or less)
                new_snooze_ms = base_snooze_ms
                print("---> [LOGIC] Normal reaction time. Granting standard 3 min snooze.")

            # 3. If they are sluggish
            else:
                # Slow reaction (more than 10 seconds)
                new_snooze_ms = penalty_snooze_ms
                print("---> [LOGIC] Sluggish baseline. Penalty applied: 2 min snooze.")

            # 5. Build the JSON Response for the ESP32
            response_payload = {
                "Status": "Success",
                "Average7Day_ms": avg_7_day,
                "NewSnoozeTime_ms": new_snooze_ms
            }

            print(f"---> [REPLY] Sending back to ESP32: {json.dumps(response_payload)}")
            print("="*50 + "\n")

            cursor.close()
            conn.close()

            # Return the calculated JSON
            return func.HttpResponse(
                body=json.dumps(response_payload),
                mimetype="application/json",
                status_code=200
            )
            
        else:
            # If it was an ALARM_FIRING or TIMER_DONE event, we don't need to do math.
            print("---> [REPLY] Non-Snooze event logged successfully.")
            print("="*50 + "\n")
            cursor.close()
            conn.close()
            return func.HttpResponse("Reaction logged successfully", status_code=200)

    except Exception as e:
        print(f"\n---> [CRITICAL ERROR] The function crashed: {str(e)}")
        print("="*50 + "\n")
        logging.error(f"Database error: {e}")
        return func.HttpResponse("Failed to log data", status_code=500)