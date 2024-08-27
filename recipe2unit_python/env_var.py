from dataclasses import dataclass
from typing import Optional


@dataclass
class EnvironmentVariables:
    thing_name: Optional[str] = ""
    aws_region: Optional[str] = ""
    ggc_version: Optional[str] = ""
    gg_root_ca_path: Optional[str] = ""
    socket_path: Optional[str] = ""
    aws_container_auth_token: Optional[str] = ""
    aws_container_cred_url: Optional[str] = ""
    user: Optional[str] = ""
    group: Optional[str] = ""
