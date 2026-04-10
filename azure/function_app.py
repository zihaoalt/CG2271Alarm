import azure.functions as func
import logging
import pyodbc
import os

app = func.FunctionApp(http_auth_level=func.AuthLevel.ANONYMOUS)

@app.route(route="log_alarm")
def log_alarm(req: func.HttpRequest) -> func.HttpResponse:
    logging.info('Python HTTP trigger function processed a request.')

    try:
        req_body = req.get_json()
        action_type = req_body.get('ActionType')
        reaction_time = req_body.get('ReactionTime_ms')

        server = os.environ["SQL_SERVER"] 
        database = os.environ["SQL_DATABASE"]
        username = os.environ["SQL_USER"]
        password = os.environ["SQL_PASSWORD"]
        driver = '{ODBC Driver 18 for SQL Server}'

        connection_string = f'DRIVER={driver};SERVER={server};PORT=1433;DATABASE={database};UID={username};PWD={password}'
        
        conn = pyodbc.connect(connection_string)
        cursor = conn.cursor()
        
        insert_query = """
        INSERT INTO AlarmLogs (ActionType, ReactionTime_ms)
        VALUES (?, ?)
        """
        cursor.execute(insert_query, action_type, reaction_time)
        conn.commit()
        
        cursor.close()
        conn.close()

        return func.HttpResponse("Reaction logged successfully", status_code=200)

    except Exception as e:
        logging.error(f"Database error: {e}")
        return func.HttpResponse("Failed to log data", status_code=500)